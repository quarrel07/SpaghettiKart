#include "GeometryCatalog.h"

#include <algorithm>
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
    mLeafOrder.clear();
    mLeafSeen.clear();
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

void GeometryCatalog::Build(const char* const wrappers[], size_t sectionCount) {
    Reset();

    bool ok = sectionCount > 0;
    for (int direction = 0; direction < 4 && ok; direction++) {
        std::unordered_set<uint64_t> emittedLeaves;
        for (size_t s = 0; s < sectionCount; s++) {
            Gfx* cmds = ResolveWrapper(wrappers[s * 4 + direction]);
            if (cmds == nullptr || !MergeWrapper(cmds, mMerged[direction], emittedLeaves)) {
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

    for (uint64_t hash : mLeafOrder) {
        EnumerateLeaf((Gfx*) ResourceGetDataByCrc(hash), 0);
    }

    mBuilt = true;
}

bool GeometryCatalog::MergeWrapper(Gfx* cmds, std::vector<Gfx>& out, std::unordered_set<uint64_t>& emittedLeaves) {
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
            if (emittedLeaves.insert(hash).second) {
                Gfx call = ptr[0];
                call.words.w0 &= ~((uintptr_t) 1 << 16);
                out.push_back(call);
                out.push_back(ptr[1]);
                if (mLeafSeen.insert(hash).second) {
                    mLeafOrder.push_back(hash);
                }
            }
            if (branch) {
                return true;
            }
            ptr += 2;
            continue;
        }
        if (op == OP_DL) {
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

int32_t GeometryCatalog::AddRef(const SlotRef& slot, uint32_t offset, Vtx* v) {
    if (slot.baseIndex < 0 || v == nullptr) {
        return -1;
    }
    std::pair<uint64_t, uint16_t> key(slot.resCrc, (uint16_t) (slot.baseIndex + offset));
    auto it = mRefIndex.find(key);
    if (it != mRefIndex.end()) {
        return (int32_t) it->second;
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
