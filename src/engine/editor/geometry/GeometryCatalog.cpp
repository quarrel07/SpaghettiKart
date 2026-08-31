#include "GeometryCatalog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace GeometryEditor {

// The port compiles with F3DEX_GBI, where classic immediate opcodes (G_ENDDL,
// G_TRI1, ...) are NEGATIVE constants; every comparison against a uint8_t
// opcode byte must go through these casts or it can never match.
static constexpr uint8_t OP_ENDDL = (uint8_t) (G_ENDDL & 0xFF); // 0xB8
static constexpr uint8_t OP_DL = (uint8_t) (G_DL & 0xFF);       // 0x06
static constexpr uint8_t OP_VTX = (uint8_t) (G_VTX & 0xFF);     // 0x04
static constexpr uint8_t OP_TRI1 = (uint8_t) (G_TRI1 & 0xFF);   // 0xBF
static constexpr uint8_t OP_TRI2 = (uint8_t) (G_TRI2 & 0xFF);   // 0xB1
static constexpr uint8_t OP_QUAD = (uint8_t) (G_QUAD & 0xFF);   // 0xB5

#define GEO_OPCODE(w0) ((uint8_t) (((w0) >> 24) & 0xFF))
#define GEO_C0(w0, pos, width) (((uint32_t) (w0) >> (pos)) & ((1U << (width)) - 1U))
// A vertex-load first word above this is a cached raw pointer, not an offset
// (the interpreter rewrites the command in place after first execution).
#define GEO_PTR_THRESHOLD 0xFFFFFu

// Plausible userland pointer range; matches the interpreter's own guard
// before it treats a word as a char*/Gfx* (segmented N64 addresses and small
// integers are excluded).
static bool PlausiblePointer(uintptr_t v) {
    return v >= 0x10000 && v < ((uintptr_t) 1 << 48);
}

// Gfx-word footprint of every command that can appear in course display
// lists, from the interpreter's handlers (libultraship interpreter.cpp) and
// the binary loader's two-word set (DisplayListFactory.cpp). Unlisted
// opcodes are one word.
static int CommandWords(uint8_t op) {
    switch (op) {
        case G_SETTIMG_OTR_HASH:  // 0x20
        case G_VTX_OTR_FILEPATH:  // 0x24
        case G_DL_OTR_HASH:       // 0x31
        case G_VTX_OTR_HASH:      // 0x32
        case G_MARKER:            // 0x33
        case G_BRANCH_Z_OTR:      // 0x35
        case G_MTX_OTR:           // 0x36
        case 0x38:                // G_FILLWIDERECT
        case 0x3E:                // G_READFB
        case 0x3F:                // G_REGBLENDEDTEX
        case 0x42:                // G_MOVEMEM_OTR
        case 0x47:                // G_LOADBLOCK_WIDE
            return 2;
        case 0x37: // G_TEXRECT_WIDE
        case 0x3C: // G_IMAGERECT
        case 0x45: // G_SETTILESIZE_INTERP
        case 0xE4: // G_TEXRECT
        case 0xE5: // G_TEXRECTFLIP
            return 3;
        case 0x4A: // G_SETTILESIZE_LERP
            return 5;
        default:
            return 1;
    }
}

static uint64_t PosKey(const Vtx* v) {
    return ((uint64_t) (uint16_t) v->v.ob[0] << 32) | ((uint64_t) (uint16_t) v->v.ob[1] << 16) |
           (uint64_t) (uint16_t) v->v.ob[2];
}

void GeometryCatalog::Reset() {
    mBuilt = false;
    mFailed = false;
    for (auto& m : mMerged) {
        m.clear();
    }
    mRepaired.clear();
    mScenery.clear();
    mSceneryContext.clear();
    mSceneryBindCtx.clear();
    mSceneryTlut.clear();
    mBindByLoad.clear();
    mBindByVtx.clear();
    mBindWalked.clear();
    mRepairWalked.clear();
    mSubtreeRaceGeo.clear();
    for (auto& m : mNearMerged) {
        m.clear();
    }
    for (auto& m : mFarMerged) {
        m.clear();
    }
    mLeafOrder.clear();
    mLeafSeen.clear();
    mLeafSections.clear();
    mLeafInfo.clear();
    mLeafInfoIndex.clear();
    mCurrentLeaf = 0;
    mMergeSection = -1;
    mRefIndex.clear();
    mVertices.clear();
    mEdges.clear();
    mEdgeSeen.clear();
    mWeldByPos.clear();
    mWeldGroups = 0;
    for (auto& s : mSlots) {
        s = SlotRef{};
    }
    for (auto& d : mSlotData) {
        d = nullptr;
    }
}

Gfx* GeometryCatalog::ResolveWrapper(const char* path) {
    if (path == nullptr || !PlausiblePointer((uintptr_t) path)) {
        return nullptr;
    }
    return (Gfx*) ResourceGetDataByName(path);
}

void GeometryCatalog::Build(const char* const wrappers[], size_t sectionCount, Gfx* creditsRoot,
                            const std::vector<SectionCentroid>& sectionCentroids) {
    Reset();

    bool ok = sectionCount > 0;
    for (int direction = 0; direction < 4 && ok; direction++) {
        std::unordered_set<uint64_t> emittedLeaves;
        for (size_t s = 0; s < sectionCount; s++) {
            mMergeSection = (int) s + 1;
            Gfx* cmds = ResolveWrapper(wrappers[s * 4 + direction]);
            if (cmds == nullptr || !MergeWrapper(cmds, mMerged[direction], emittedLeaves, 0)) {
                ok = false;
                break;
            }
        }
        Gfx end;
        end.words.w0 = (uintptr_t) OP_ENDDL << 24;
        end.words.w1 = 0;
        mMerged[direction].push_back(end);
    }

    if (!ok || mLeafOrder.empty()) {
        mFailed = true;
        printf("[GeometryMode] catalog build failed; falling back to stock rendering\n");
        return;
    }

    if (creditsRoot != nullptr) {
        mCurrentLeaf = 0;
        EnumerateLeaf(creditsRoot, 0);
        // Second pass: flag which catalog vertices the racing lists also
        // reference (for the inspector warning) and attribute a bounding box
        // to every racing leaf for LOD classification.
        mMarkOnly = true;
        for (uint64_t hash : mLeafOrder) {
            mCurrentLeaf = hash;
            LeafInfo info;
            info.hash = hash;
            mLeafInfoIndex.emplace(hash, mLeafInfo.size());
            mLeafInfo.push_back(info);
            EnumerateLeaf((Gfx*) ResourceGetDataByCrc(hash), 0);
        }
        mMarkOnly = false;
        mCurrentLeaf = 0;
        ClassifyLeaves(sectionCentroids);
        // Filtered sweeps for the Force LOD views, now that classes exist.
        for (int filter = 1; filter <= 2; filter++) {
            auto* dest = (filter == 1) ? mNearMerged : mFarMerged;
            for (int direction = 0; direction < 4; direction++) {
                std::unordered_set<uint64_t> emitted;
                for (size_t s = 0; s < sectionCount; s++) {
                    mMergeSection = -1;
                    Gfx* cmds = ResolveWrapper(wrappers[s * 4 + direction]);
                    if (cmds != nullptr) {
                        MergeWrapper(cmds, dest[direction], emitted, filter);
                    }
                }
                Gfx end;
                end.words.w0 = (uintptr_t) OP_ENDDL << 24;
                end.words.w1 = 0;
                dest[direction].push_back(end);
            }
        }
    } else {
        for (uint64_t hash : mLeafOrder) {
            EnumerateLeaf((Gfx*) ResourceGetDataByCrc(hash), 0);
        }
        for (auto& r : mVertices) {
            r.drawnInRace = true;
        }
    }
    if (mVertices.empty()) {
        mFailed = true;
        printf("[GeometryMode] catalog enumerated no vertices; falling back to stock rendering\n");
        return;
    }

    if (creditsRoot != nullptr) {
        // The scenery layer draws before everything else, so it must open
        // with the assembly root's own leading state block (combine mode
        // and friends) - the authored state its pieces were written under.
        // Frame-dump evidence: without this the scenery runs under the
        // frame preamble's combine (FC121824) instead of the course's
        // (FC127E24), rendering signs as solid color.
        {
            Gfx* ptr = creditsRoot;
            for (int guard = 0; guard < 4096; guard++) {
                uint8_t op = GEO_OPCODE(ptr->words.w0);
                if (op == OP_ENDDL || op == G_DL_OTR_HASH || op == OP_DL) {
                    break;
                }
                if (op == G_MARKER) {
                    ptr += 2;
                    continue;
                }
                int w = CommandWords(op);
                for (int i = 0; i < w; i++) {
                    mScenery.push_back(ptr[i]);
                }
                ptr += w;
            }
        }
        // Capture the texture binds the racing tables author for every
        // vertex load, then rebuild the full-course assembly with those
        // binds injected so no surface depends on inherited state.
        std::vector<Gfx> bindBuffer;
        for (int direction = 0; direction < 4; direction++) {
            for (size_t sec = 0; sec < sectionCount; sec++) {
                Gfx* cmds = ResolveWrapper(wrappers[sec * 4 + direction]);
                if (cmds != nullptr) {
                    CaptureRaceBinds(cmds, bindBuffer, 0);
                }
            }
        }
        size_t preambleOnly = mScenery.size();
        BuildRepairedFullCourse(creditsRoot, 0);
        Gfx end;
        end.words.w0 = (uintptr_t) OP_ENDDL << 24;
        end.words.w1 = 0;
        mRepaired.push_back(end);
        if (mScenery.size() > preambleOnly) {
            mScenery.push_back(end);
        } else {
            mScenery.clear(); // no scenery pieces: no layer at all
        }
        printf("[GeometryMode] repaired full-course assembly: %zu words, %zu bind snapshots\n", mRepaired.size(),
               mBindByLoad.size());
    }
    mBuilt = true;
}

bool GeometryCatalog::MergeWrapper(Gfx* cmds, std::vector<Gfx>& out, std::unordered_set<uint64_t>& emittedLeaves,
                                   int lodFilter) {
    Gfx* ptr = cmds;
    for (int guard = 0; guard < (1 << 16); guard++) {
        uintptr_t w0 = ptr->words.w0;
        uint8_t op = GEO_OPCODE(w0);
        // Branch (no-push) calls would let the callee's end command terminate
        // our merged list, so they are emitted as plain calls and end the
        // wrapper (nothing after a branch is reachable anyway).
        bool branch = GEO_C0(w0, 16, 1) != 0;

        if (op == OP_ENDDL) {
            return true;
        }
        if (op == G_MARKER) {
            ptr += 2;
            continue;
        }
        if (op == G_DL_OTR_HASH) {
            uint64_t hash = ((uint64_t) ptr[1].words.w0 << 32) + ptr[1].words.w1;
            if (mMergeSection > 0) {
                mLeafSections[hash].insert(mMergeSection);
            }
            bool allowed = true;
            if (lodFilter != 0) {
                auto li = mLeafInfoIndex.find(hash);
                if (li != mLeafInfoIndex.end()) {
                    const LeafInfo& info = mLeafInfo[li->second];
                    if (lodFilter == 1) {
                        // Force No LOD: hide pieces only ever drawn from far
                        // away; every area keeps its own section's version.
                        allowed = info.cls != LeafClass::Far;
                    } else {
                        // Force LOD: each area drawn exactly as its farthest
                        // viewing section draws it, per the visibility table.
                        allowed = info.drawnByFarthest;
                    }
                }
            }
            if (allowed && emittedLeaves.insert(hash).second) {
                Gfx call = ptr[0];
                call.words.w0 &= ~((uintptr_t) 1 << 16);
                out.push_back(call);
                out.push_back(ptr[1]);
                if (mLeafSeen.insert(hash).second) {
                    mLeafOrder.push_back(hash);
                }
            } else {
                // Course pieces inherit texture state from their in-list
                // predecessors, so a dropped duplicate must still contribute
                // its state commands (just not its triangles) or the next
                // piece draws with whatever texture happens to be loaded.
                EmitLeafStateOnly((Gfx*) ResourceGetDataByCrc(hash), out, 0);
            }
            if (branch) {
                return true;
            }
            ptr += 2;
            continue;
        }
        if (op == OP_DL) {
            if (PlausiblePointer(ptr->words.w1)) {
                if (emittedLeaves.insert((uint64_t) ptr->words.w1).second) {
                    Gfx call = ptr[0];
                    call.words.w0 &= ~((uintptr_t) 1 << 16);
                    out.push_back(call);
                } else {
                    EmitLeafStateOnly((Gfx*) ptr->words.w1, out, 0);
                }
            }
            if (branch) {
                return true;
            }
            ptr += 1;
            continue;
        }
        if (op == G_DL_OTR_FILEPATH) {
            if (PlausiblePointer(ptr->words.w1) && emittedLeaves.insert((uint64_t) ptr->words.w1).second) {
                Gfx call = ptr[0];
                call.words.w0 &= ~((uintptr_t) 1 << 16);
                out.push_back(call);
            }
            if (branch) {
                return true;
            }
            ptr += 1;
            continue;
        }
        if (op == G_BRANCH_Z_OTR) {
            // Conditional LOD jump; skipping it keeps the merged list linear
            // (the fall-through geometry still draws).
            ptr += 2;
            continue;
        }
        // Any other state command: copy verbatim, full width.
        int words = CommandWords(op);
        for (int i = 0; i < words; i++) {
            out.push_back(ptr[i]);
        }
        ptr += words;
    }
    return false; // guard exhausted: not a well-formed wrapper
}

// Classifies each racing leaf from the visibility table itself: which
// camera sections draw it, and how far those cameras stand from the leaf.
// Drawn from up close = the area's own up-close version; only ever drawn
// from far away = a distance representation. The cutoffs are expressed in
// units of the course's own median section spacing, and anything landing
// between them is flagged Ambiguous and kept visible in both Force modes.
void GeometryCatalog::ClassifyLeaves(const std::vector<SectionCentroid>& sectionCentroids) {
    if (sectionCentroids.empty()) {
        return;
    }
    std::unordered_map<int, const SectionCentroid*> centroidById;
    for (const auto& sc : sectionCentroids) {
        centroidById[sc.sectionId] = &sc;
    }
    // Median distance between consecutive sections along the track path.
    std::vector<float> spacings;
    for (size_t i = 0; i + 1 < sectionCentroids.size(); i++) {
        const auto& a = sectionCentroids[i];
        const auto& b = sectionCentroids[i + 1];
        float dx = a.pos[0] - b.pos[0], dy = a.pos[1] - b.pos[1], dz = a.pos[2] - b.pos[2];
        spacings.push_back(std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    std::sort(spacings.begin(), spacings.end());
    float medianSpacing = spacings.empty() ? 800.0f : spacings[spacings.size() / 2];
    if (medianSpacing < 1.0f) {
        medianSpacing = 800.0f;
    }
    const float nearCut = 1.5f * medianSpacing;
    const float farCut = 2.5f * medianSpacing;

    // Distance from a camera section's centroid to the leaf's bounding box
    // (closest point, so long walls measure from their nearest edge).
    auto boxDist = [](const LeafInfo& li, const float* p) {
        float d2 = 0.0f;
        for (int i = 0; i < 3; i++) {
            float v = p[i] < li.boxMin[i] ? li.boxMin[i] - p[i] : (p[i] > li.boxMax[i] ? p[i] - li.boxMax[i] : 0.0f);
            d2 += v * v;
        }
        return std::sqrt(d2);
    };

    for (auto& li : mLeafInfo) {
        if (!li.hasBox) {
            li.cls = LeafClass::Unplaced;
            continue;
        }
        float c[3] = { (li.boxMin[0] + li.boxMax[0]) * 0.5f, (li.boxMin[1] + li.boxMax[1]) * 0.5f,
                       (li.boxMin[2] + li.boxMax[2]) * 0.5f };
        float best = 1e30f;
        for (const auto& sc : sectionCentroids) {
            float dx = c[0] - sc.pos[0], dy = c[1] - sc.pos[1], dz = c[2] - sc.pos[2];
            float d = dx * dx + dy * dy + dz * dz;
            if (d < best) {
                best = d;
                li.homeSection = sc.sectionId;
            }
        }
        auto it = mLeafSections.find(li.hash);
        if (it == mLeafSections.end() || it->second.empty()) {
            // Never listed by any section we merged; keep it everywhere.
            li.cls = LeafClass::Ambiguous;
            continue;
        }
        li.viewerCount = (int) it->second.size();
        li.inHomeList = it->second.count(li.homeSection) != 0;
        float nearest = 1e30f;
        for (int v : it->second) {
            auto cIt = centroidById.find(v);
            if (cIt != centroidById.end()) {
                nearest = std::min(nearest, boxDist(li, cIt->second->pos));
            }
        }
        li.nearestViewerDist = nearest;
        li.viewerRatio = nearest / medianSpacing;
        if (li.inHomeList || nearest <= nearCut) {
            li.cls = LeafClass::Near;
        } else if (nearest >= farCut) {
            li.cls = LeafClass::Far;
        } else {
            li.cls = LeafClass::Ambiguous;
        }
    }

    // Force LOD view: each area rendered exactly as its farthest viewing
    // section renders it. Per home section, find that farthest viewer and
    // keep the leaves it draws; Ambiguous/Unplaced leaves always stay.
    std::unordered_map<int, std::vector<size_t>> byHome;
    for (size_t i = 0; i < mLeafInfo.size(); i++) {
        if (mLeafInfo[i].hasBox && mLeafInfo[i].homeSection >= 0) {
            byHome[mLeafInfo[i].homeSection].push_back(i);
        }
    }
    for (auto& [home, indices] : byHome) {
        auto hIt = centroidById.find(home);
        if (hIt == centroidById.end()) {
            continue;
        }
        int farthest = -1;
        float farthestDist = -1.0f;
        for (size_t idx : indices) {
            auto vIt = mLeafSections.find(mLeafInfo[idx].hash);
            if (vIt == mLeafSections.end()) {
                continue;
            }
            for (int v : vIt->second) {
                auto cIt = centroidById.find(v);
                if (cIt == centroidById.end()) {
                    continue;
                }
                float dx = cIt->second->pos[0] - hIt->second->pos[0];
                float dy = cIt->second->pos[1] - hIt->second->pos[1];
                float dz = cIt->second->pos[2] - hIt->second->pos[2];
                float d = std::sqrt(dx * dx + dy * dy + dz * dz);
                if (d > farthestDist) {
                    farthestDist = d;
                    farthest = v;
                }
            }
        }
        for (size_t idx : indices) {
            auto& li = mLeafInfo[idx];
            if (li.cls == LeafClass::Ambiguous || li.cls == LeafClass::Unplaced) {
                li.drawnByFarthest = true;
                continue;
            }
            auto vIt = mLeafSections.find(li.hash);
            li.drawnByFarthest = farthest >= 0 && vIt != mLeafSections.end() && vIt->second.count(farthest) != 0;
        }
    }

    size_t nNear = 0, nFar = 0, nAmbiguous = 0, nUnplaced = 0;
    for (const auto& li : mLeafInfo) {
        switch (li.cls) {
            case LeafClass::Near: nNear++; break;
            case LeafClass::Far: nFar++; break;
            case LeafClass::Ambiguous: nAmbiguous++; break;
            case LeafClass::Unplaced: nUnplaced++; break;
        }
    }
    printf("[GeometryMode] LOD classification: %zu near, %zu far, %zu ambiguous, %zu unplaced (spacing %.0f)\n", nNear,
           nFar, nAmbiguous, nUnplaced, medianSpacing);
    // Also to a file, since a Finder-launched app has no visible console.
    FILE* f = fopen("/Users/Andy/Documents/GitHub/Repo-Clones/SpaghettiKart-geometry/Claude Test Builds/"
                    "lod_classification.txt",
                    "w");
    if (f != nullptr) {
        fprintf(f, "near=%zu far=%zu ambiguous=%zu unplaced=%zu\n", nNear, nFar, nAmbiguous, nUnplaced);
        fprintf(f, "median section spacing=%.0f nearCut=%.0f farCut=%.0f\n", medianSpacing, nearCut, farCut);
        fprintf(f, "columns: class home inHome viewers nearestViewerDist(ratio) inFarView name\n\n");
        if (nAmbiguous > 0) {
            fprintf(f, "AMBIGUOUS (kept visible in both Force modes; judgment calls):\n");
            for (const auto& li : mLeafInfo) {
                if (li.cls == LeafClass::Ambiguous) {
                    const char* n = ResourceGetNameByCrc(li.hash);
                    fprintf(f, "  home=%-3d viewers=%-2d dist=%.0f(%.2fx) %s\n", li.homeSection, li.viewerCount,
                            li.nearestViewerDist, li.viewerRatio, n != nullptr ? n : "?");
                }
            }
            fprintf(f, "\n");
        }
        for (const auto& li : mLeafInfo) {
            const char* n = ResourceGetNameByCrc(li.hash);
            const char* cls = li.cls == LeafClass::Near        ? "near"
                              : li.cls == LeafClass::Far       ? "FAR"
                              : li.cls == LeafClass::Ambiguous ? "AMBIG"
                                                               : "unplaced";
            fprintf(f, "%-8s home=%-3d inHome=%d viewers=%-2d dist=%.0f(%.2fx) inFarView=%d %s\n", cls,
                    li.homeSection, li.inHomeList ? 1 : 0, li.viewerCount, li.nearestViewerDist, li.viewerRatio,
                    li.drawnByFarthest ? 1 : 0, n != nullptr ? n : "?");
        }
        fclose(f);
    }
}

// True for commands that participate in texture binding: replaying the most
// recent run of these reconstructs the texture state a surface was authored
// against. (SETTIMG pairs, tile setup/loads, TLUT, syncs, combine.)
static bool IsBindOp(uint8_t op) {
    switch (op) {
        case G_SETTIMG_OTR_HASH: // 0x20, two words
        case 0xF0:               // G_LOADTLUT
        case 0xF2:               // G_SETTILESIZE
        case 0xF3:               // G_LOADBLOCK
        case 0xF4:               // G_LOADTILE
        case 0xF5:               // G_SETTILE
        case 0xE6:               // G_RDPLOADSYNC
        case 0xE7:               // G_RDPPIPESYNC
        case 0xE8:               // G_RDPTILESYNC
        case 0xFC:               // G_SETCOMBINE
            return true;
        default:
            return false;
    }
}

// Walks a racing sheet keeping a rolling buffer of bind commands; at every
// vertex load, snapshots the buffer as that load's authored texture state.
// First snapshot wins (the first drawn context is the up-close one).
void GeometryCatalog::CaptureRaceBinds(Gfx* cmds, std::vector<Gfx>& buffer, int depth) {
    if (cmds == nullptr || depth > 10) {
        return;
    }
    Gfx* ptr = cmds;
    for (int guard = 0; guard < (1 << 18); guard++) {
        uintptr_t w0 = ptr->words.w0;
        uint8_t op = GEO_OPCODE(w0);
        if (op == OP_ENDDL) {
            return;
        }
        if (op == G_MARKER) {
            ptr += 2;
            continue;
        }
        if (op == G_DL_OTR_HASH) {
            uint64_t hash = ((uint64_t) ptr[1].words.w0 << 32) + ptr[1].words.w1;
            if (mBindWalked.insert(hash).second) {
                CaptureRaceBinds((Gfx*) ResourceGetDataByCrc(hash), buffer, depth + 1);
            }
            if (GEO_C0(w0, 16, 1) != 0) {
                return;
            }
            ptr += 2;
            continue;
        }
        if (op == G_VTX_OTR_HASH) {
            uint64_t hash = ((uint64_t) ptr[1].words.w0 << 32) + ptr[1].words.w1;
            if (PlausiblePointer(hash) || ptr[1].words.w0 != 0) { // still hash form
                auto key = std::make_pair(hash, (uint32_t) w0);
                if (mBindByLoad.find(key) == mBindByLoad.end()) {
                    mBindByLoad.emplace(key, buffer);
                }
                if (mBindByVtx.find(hash) == mBindByVtx.end()) {
                    mBindByVtx.emplace(hash, buffer);
                }
            }
            ptr += 2;
            continue;
        }
        if (IsBindOp(op)) {
            int words = CommandWords(op);
            for (int i = 0; i < words; i++) {
                buffer.push_back(ptr[i]);
            }
            // Cap the rolling buffer; later commands override earlier ones
            // on replay. Trim WHOLE commands from the front: erasing single
            // words can orphan the second half of a two-word command, and an
            // orphaned hash word replayed as an instruction desyncs the
            // interpreter (proven crash: 69 of Moo Moo's 342 snapshots).
            while (buffer.size() > 48) {
                size_t drop = (size_t) CommandWords(GEO_OPCODE(buffer.front().words.w0));
                if (drop > buffer.size()) {
                    drop = buffer.size();
                }
                buffer.erase(buffer.begin(), buffer.begin() + drop);
            }
            ptr += words;
            continue;
        }
        ptr += CommandWords(op);
    }
}

// True if the subtree draws at least one vertex load the race tables also
// author (or draws nothing at all, e.g. pure state wrappers, which are kept).
bool GeometryCatalog::SubtreeHasRaceGeometry(Gfx* cmds, int depth) {
    if (cmds == nullptr || depth > 10) {
        return false;
    }
    bool sawLoad = false;
    Gfx* ptr = cmds;
    for (int guard = 0; guard < (1 << 18); guard++) {
        uintptr_t w0 = ptr->words.w0;
        uint8_t op = GEO_OPCODE(w0);
        if (op == OP_ENDDL) {
            break;
        }
        if (op == G_MARKER) {
            ptr += 2;
            continue;
        }
        if (op == G_DL_OTR_HASH) {
            uint64_t hash = ((uint64_t) ptr[1].words.w0 << 32) + ptr[1].words.w1;
            auto it = mSubtreeRaceGeo.find(hash);
            bool sub;
            if (it != mSubtreeRaceGeo.end()) {
                sub = it->second;
            } else {
                sub = SubtreeHasRaceGeometry((Gfx*) ResourceGetDataByCrc(hash), depth + 1);
                mSubtreeRaceGeo.emplace(hash, sub);
            }
            if (sub) {
                return true;
            }
            sawLoad = true; // subtree drew something, all of it twinless
            if (GEO_C0(w0, 16, 1) != 0) {
                break;
            }
            ptr += 2;
            continue;
        }
        if (op == G_VTX_OTR_HASH) {
            uint64_t hash = ((uint64_t) ptr[1].words.w0 << 32) + ptr[1].words.w1;
            sawLoad = true;
            if (mBindByVtx.find(hash) != mBindByVtx.end()) {
                return true;
            }
            ptr += 2;
            continue;
        }
        ptr += CommandWords(op);
    }
    return !sawLoad; // no geometry at all: keep (harmless state/structure)
}

// Flattens the credits assembly into mRepaired, injecting each vertex
// load's race-authored bind block right before it. Loads without a race
// twin (credits-only scenery) keep their own authored state.
void GeometryCatalog::BuildRepairedFullCourse(Gfx* creditsRoot, int depth) {
    if (creditsRoot == nullptr || depth > 10) {
        return;
    }
    Gfx* ptr = creditsRoot;
    for (int guard = 0; guard < (1 << 18); guard++) {
        uintptr_t w0 = ptr->words.w0;
        uint8_t op = GEO_OPCODE(w0);
        if (op == OP_ENDDL) {
            return;
        }
        if (op == G_MARKER) {
            ptr += 2;
            continue;
        }
        if (op == G_DL_OTR_HASH) {
            uint64_t hash = ((uint64_t) ptr[1].words.w0 << 32) + ptr[1].words.w1;
            if (mRepairWalked.insert(hash).second) {
                Gfx* child = (Gfx*) ResourceGetDataByCrc(hash);
                if (SubtreeHasRaceGeometry(child, depth + 1)) {
                    BuildRepairedFullCourse(child, depth + 1);
                } else {
                    // Credits-only scenery: out of the course assembly, into
                    // the optional scenery layer. Prefix the global enables
                    // in effect at this point of the authored stream, so the
                    // detached piece renders as it did in place (textured
                    // arrows, not untextured rectangles).
                    Gfx sync;
                    sync.words.w0 = (uintptr_t) 0xE7 << 24; // G_RDPPIPESYNC
                    sync.words.w1 = 0;
                    mScenery.push_back(sync);
                    mScenery.insert(mScenery.end(), mSceneryContext.begin(), mSceneryContext.end());
                    mScenery.insert(mScenery.end(), mSceneryBindCtx.begin(), mSceneryBindCtx.end());
                    // Palette last so nothing in the bind context clobbers it:
                    // the pieces' own texture loads assume this TLUT resident.
                    mScenery.insert(mScenery.end(), mSceneryTlut.begin(), mSceneryTlut.end());
                    CopySubtree(child, mScenery, depth + 1);
                }
            }
            if (GEO_C0(w0, 16, 1) != 0) {
                return;
            }
            ptr += 2;
            continue;
        }
        if (op == G_VTX_OTR_HASH) {
            uint64_t hash = ((uint64_t) ptr[1].words.w0 << 32) + ptr[1].words.w1;
            auto it = mBindByLoad.find(std::make_pair(hash, (uint32_t) w0));
            const std::vector<Gfx>* bind = nullptr;
            if (it != mBindByLoad.end()) {
                bind = &it->second;
            } else {
                auto iv = mBindByVtx.find(hash);
                if (iv != mBindByVtx.end()) {
                    bind = &iv->second;
                }
            }
            if (bind != nullptr) {
                mRepaired.insert(mRepaired.end(), bind->begin(), bind->end());
            }
            mRepaired.push_back(ptr[0]);
            mRepaired.push_back(ptr[1]);
            ptr += 2;
            continue;
        }
        int words = CommandWords(op);
        for (int i = 0; i < words; i++) {
            mRepaired.push_back(ptr[i]);
        }
        ptr += words;
    }
}

// Verbatim flattened copy of a subtree (calls inlined, markers stripped).
void GeometryCatalog::CopySubtree(Gfx* cmds, std::vector<Gfx>& out, int depth) {
    if (cmds == nullptr || depth > 10) {
        return;
    }
    Gfx* ptr = cmds;
    for (int guard = 0; guard < (1 << 18); guard++) {
        uintptr_t w0 = ptr->words.w0;
        uint8_t op = GEO_OPCODE(w0);
        if (op == OP_ENDDL) {
            return;
        }
        if (op == G_MARKER) {
            ptr += 2;
            continue;
        }
        if (op == G_DL_OTR_HASH) {
            uint64_t hash = ((uint64_t) ptr[1].words.w0 << 32) + ptr[1].words.w1;
            CopySubtree((Gfx*) ResourceGetDataByCrc(hash), out, depth + 1);
            if (GEO_C0(w0, 16, 1) != 0) {
                return;
            }
            ptr += 2;
            continue;
        }
        int words = CommandWords(op);
        for (int i = 0; i < words; i++) {
            out.push_back(ptr[i]);
        }
        ptr += words;
    }
}

// Copies a dropped leaf's non-drawing commands (texture loads, tile setup,
// modes) into the merged list so later pieces inherit the state their
// authored order relies on. Triangles and vertex loads are skipped.
void GeometryCatalog::EmitLeafStateOnly(Gfx* cmds, std::vector<Gfx>& out, int depth) {
    if (cmds == nullptr || depth > 8) {
        return;
    }
    Gfx* ptr = cmds;
    for (int guard = 0; guard < (1 << 18); guard++) {
        uintptr_t w0 = ptr->words.w0;
        uintptr_t w1 = ptr->words.w1;
        uint8_t op = GEO_OPCODE(w0);

        if (op == OP_ENDDL) {
            return;
        }
        switch (op) {
            case G_MARKER:
            case G_BRANCH_Z_OTR:
                break; // skip, no state contribution we replay
            case G_DL_OTR_HASH: {
                uint64_t hash = ((uint64_t) ptr[1].words.w0 << 32) + ptr[1].words.w1;
                EmitLeafStateOnly((Gfx*) ResourceGetDataByCrc(hash), out, depth + 1);
                if (GEO_C0(w0, 16, 1) != 0) {
                    return;
                }
                break;
            }
            case OP_DL:
                if (PlausiblePointer(w1)) {
                    EmitLeafStateOnly((Gfx*) w1, out, depth + 1);
                }
                if (GEO_C0(w0, 16, 1) != 0) {
                    return;
                }
                break;
            case G_DL_OTR_FILEPATH:
                if (PlausiblePointer(w1)) {
                    EmitLeafStateOnly((Gfx*) ResourceGetDataByName((const char*) w1), out, depth + 1);
                }
                if (GEO_C0(w0, 16, 1) != 0) {
                    return;
                }
                break;
            case G_VTX_OTR_HASH:
            case G_VTX_OTR_FILEPATH:
            case 0x48: // G_VTX_WIDE
            case OP_VTX:
            case G_TRI1_OTR:
            case OP_TRI1:
            case OP_TRI2:
            case OP_QUAD:
                break; // drawing commands: dropped
            default: {
                int words = CommandWords(op);
                for (int i = 0; i < words; i++) {
                    out.push_back(ptr[i]);
                }
                break;
            }
        }
        ptr += CommandWords(op);
    }
}

int32_t GeometryCatalog::AddRef(const SlotRef& slot, uint32_t offset, Vtx* v) {
    if (slot.baseIndex < 0 || v == nullptr) {
        return -1;
    }
    if (mCurrentLeaf != 0) {
        auto it = mLeafInfoIndex.find(mCurrentLeaf);
        if (it != mLeafInfoIndex.end()) {
            LeafInfo& li = mLeafInfo[it->second];
            float p[3] = { (float) v->v.ob[0], (float) v->v.ob[1], (float) v->v.ob[2] };
            if (!li.hasBox) {
                for (int i = 0; i < 3; i++) {
                    li.boxMin[i] = li.boxMax[i] = p[i];
                }
                li.hasBox = true;
            } else {
                for (int i = 0; i < 3; i++) {
                    li.boxMin[i] = std::min(li.boxMin[i], p[i]);
                    li.boxMax[i] = std::max(li.boxMax[i], p[i]);
                }
            }
        }
    }
    std::pair<uint64_t, uint16_t> key(slot.resCrc, (uint16_t) (slot.baseIndex + offset));
    auto it = mRefIndex.find(key);
    if (it != mRefIndex.end()) {
        if (mMarkOnly) {
            mVertices[it->second].drawnInRace = true;
        }
        return (int32_t) it->second;
    }
    if (mMarkOnly) {
        return -1; // marking pass never creates refs
    }
    VertexRef ref;
    ref.resCrc = key.first;
    ref.index = key.second;
    ref.pos[0] = v->v.ob[0];
    ref.pos[1] = v->v.ob[1];
    ref.pos[2] = v->v.ob[2];
    ref.live = v;
    uint64_t pk = PosKey(v);
    auto weld = mWeldByPos.find(pk);
    if (weld == mWeldByPos.end()) {
        ref.weldGroup = mWeldGroups++;
        mWeldByPos.emplace(pk, ref.weldGroup);
    } else {
        ref.weldGroup = weld->second;
    }
    mRefIndex.emplace(key, mVertices.size());
    mVertices.push_back(ref);
    return (int32_t) (mVertices.size() - 1);
}

void GeometryCatalog::AddTriangleEdges(int32_t a, int32_t b, int32_t c) {
    const int32_t idx[3][2] = { { a, b }, { b, c }, { c, a } };
    for (const auto& e : idx) {
        if (e[0] < 0 || e[1] < 0 || e[0] == e[1]) {
            continue;
        }
        uint32_t lo = (uint32_t) std::min(e[0], e[1]);
        uint32_t hi = (uint32_t) std::max(e[0], e[1]);
        if (mEdgeSeen.insert(((uint64_t) lo << 32) | hi).second) {
            mEdges.emplace_back(lo, hi);
        }
    }
    // Accumulate the face normal on each corner for back-face filtering.
    if (a >= 0 && b >= 0 && c >= 0) {
        VertexRef& va = mVertices[a];
        VertexRef& vb = mVertices[b];
        VertexRef& vc = mVertices[c];
        float e1[3] = { (float) (vb.pos[0] - va.pos[0]), (float) (vb.pos[1] - va.pos[1]),
                        (float) (vb.pos[2] - va.pos[2]) };
        float e2[3] = { (float) (vc.pos[0] - va.pos[0]), (float) (vc.pos[1] - va.pos[1]),
                        (float) (vc.pos[2] - va.pos[2]) };
        float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                       e1[0] * e2[1] - e1[1] * e2[0] };
        for (VertexRef* v : { &va, &vb, &vc }) {
            v->nrm[0] += n[0];
            v->nrm[1] += n[1];
            v->nrm[2] += n[2];
        }
    }
}

void GeometryCatalog::AddVertexRange(uint64_t crc, int32_t baseIndex, uint32_t dest, uint32_t count, Vtx* data) {
    for (uint32_t i = 0; i < count && dest + i < 64; i++) {
        mSlots[dest + i].resCrc = crc;
        mSlots[dest + i].baseIndex = (baseIndex < 0) ? -1 : baseIndex + (int32_t) i;
        mSlotData[dest + i] = (data == nullptr) ? nullptr : data + i;
    }
}

void GeometryCatalog::EnumerateLeaf(Gfx* cmds, int depth) {
    if (cmds == nullptr || depth > 8) {
        return;
    }
    Gfx* ptr = cmds;
    for (int guard = 0; guard < (1 << 18); guard++) {
        uintptr_t w0 = ptr->words.w0;
        uintptr_t w1 = ptr->words.w1;
        uint8_t op = GEO_OPCODE(w0);

        if (op == OP_ENDDL) {
            return;
        }

        switch (op) {
            case G_DL_OTR_HASH: {
                uint64_t hash = ((uint64_t) ptr[1].words.w0 << 32) + ptr[1].words.w1;
                EnumerateLeaf((Gfx*) ResourceGetDataByCrc(hash), depth + 1);
                break;
            }
            case OP_DL:
                if (PlausiblePointer(w1)) {
                    EnumerateLeaf((Gfx*) w1, depth + 1);
                }
                if (GEO_C0(w0, 16, 1) != 0) {
                    return; // branch: control never returns here
                }
                break;
            case G_DL_OTR_FILEPATH:
                if (PlausiblePointer(w1)) {
                    EnumerateLeaf((Gfx*) ResourceGetDataByName((const char*) w1), depth + 1);
                }
                if (GEO_C0(w0, 16, 1) != 0) {
                    return;
                }
                break;
            case G_BRANCH_Z_OTR:
                // The merged render path never takes the LOD branch, so its
                // target geometry would only produce phantom floating
                // vertices; skip it.
                break;
            case G_VTX_OTR_HASH: {
                uint64_t hash = ((uint64_t) ptr[1].words.w0 << 32) + ptr[1].words.w1;
                Vtx* base = (Vtx*) ResourceGetDataByCrc(hash);
                uint32_t count = GEO_C0(w0, 12, 8);
                uint32_t dest = GEO_C0(w0, 1, 7) - count;
                if (base != nullptr) {
                    uintptr_t byteOff =
                        (w1 > GEO_PTR_THRESHOLD) ? (uintptr_t) ((char*) w1 - (char*) base) : (uintptr_t) w1;
                    int32_t baseIndex = (int32_t) (byteOff / sizeof(Vtx));
                    AddVertexRange(hash, baseIndex, dest, count, (Vtx*) ((char*) base + byteOff));
                }
                break;
            }
            case G_VTX_OTR_FILEPATH: {
                uint32_t count = (uint32_t) ptr[1].words.w0;
                uint32_t dest = (uint32_t) (ptr[1].words.w1 >> 16);
                AddVertexRange(0, -1, dest, count, nullptr);
                break;
            }
            case OP_VTX: {
                // Raw f3dex vertex load: count and destination use the f3dex
                // field layout, not the OTR one.
                uint32_t count = GEO_C0(w0, 10, 6);
                uint32_t dest = GEO_C0(w0, 17, 7);
                AddVertexRange(0, -1, dest, count, nullptr);
                break;
            }
            case 0x48: { // G_VTX_WIDE (f3dex2 field layout)
                uint32_t count = GEO_C0(w0, 12, 8);
                uint32_t dest = GEO_C0(w0, 1, 7) - count;
                AddVertexRange(0, -1, dest, count, nullptr);
                break;
            }
            case G_TRI1_OTR: {
                uint32_t v1 = (uint32_t) (w0 & 0xFFFF);
                uint32_t v2 = (uint32_t) (w1 >> 16) & 0xFFFF;
                uint32_t v3 = (uint32_t) (w1 & 0xFFFF);
                int32_t r1 = (v1 < 64) ? AddRef(mSlots[v1], 0, mSlotData[v1]) : -1;
                int32_t r2 = (v2 < 64) ? AddRef(mSlots[v2], 0, mSlotData[v2]) : -1;
                int32_t r3 = (v3 < 64) ? AddRef(mSlots[v3], 0, mSlotData[v3]) : -1;
                AddTriangleEdges(r1, r2, r3);
                break;
            }
            case OP_TRI1: {
                uint32_t v1 = ((uint32_t) (w1 & 0x00FF0000) >> 16) / 2;
                uint32_t v2 = ((uint32_t) (w1 & 0x0000FF00) >> 8) / 2;
                uint32_t v3 = ((uint32_t) (w1 & 0x000000FF)) / 2;
                AddTriangleEdges(AddRef(mSlots[v1], 0, mSlotData[v1]), AddRef(mSlots[v2], 0, mSlotData[v2]),
                                 AddRef(mSlots[v3], 0, mSlotData[v3]));
                break;
            }
            case OP_TRI2: {
                uint32_t v1 = ((uint32_t) (w0 & 0x00FF0000) >> 16) / 2;
                uint32_t v2 = ((uint32_t) (w0 & 0x0000FF00) >> 8) / 2;
                uint32_t v3 = ((uint32_t) (w0 & 0x000000FF)) / 2;
                uint32_t v4 = ((uint32_t) (w1 & 0x00FF0000) >> 16) / 2;
                uint32_t v5 = ((uint32_t) (w1 & 0x0000FF00) >> 8) / 2;
                uint32_t v6 = ((uint32_t) (w1 & 0x000000FF)) / 2;
                AddTriangleEdges(AddRef(mSlots[v1], 0, mSlotData[v1]), AddRef(mSlots[v2], 0, mSlotData[v2]),
                                 AddRef(mSlots[v3], 0, mSlotData[v3]));
                AddTriangleEdges(AddRef(mSlots[v4], 0, mSlotData[v4]), AddRef(mSlots[v5], 0, mSlotData[v5]),
                                 AddRef(mSlots[v6], 0, mSlotData[v6]));
                break;
            }
            case OP_QUAD: {
                uint32_t v1 = ((uint32_t) (w1 >> 24) & 0xFF) / 2;
                uint32_t v2 = ((uint32_t) (w1 >> 16) & 0xFF) / 2;
                uint32_t v3 = ((uint32_t) (w1 >> 8) & 0xFF) / 2;
                uint32_t v4 = ((uint32_t) w1 & 0xFF) / 2;
                {
                    int32_t r1 = AddRef(mSlots[v1], 0, mSlotData[v1]);
                    int32_t r2 = AddRef(mSlots[v2], 0, mSlotData[v2]);
                    int32_t r3 = AddRef(mSlots[v3], 0, mSlotData[v3]);
                    int32_t r4 = AddRef(mSlots[v4], 0, mSlotData[v4]);
                    AddTriangleEdges(r1, r2, r3);
                    AddTriangleEdges(r1, r3, r4);
                }
                break;
            }
            default:
                break;
        }
        ptr += CommandWords(op);
    }
}

} // namespace GeometryEditor
