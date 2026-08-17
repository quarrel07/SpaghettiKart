#include "GeometryCatalog.h"

#include <cstdio>

namespace GeometryEditor {

// Field decode helpers matching Fast3D's interpreter, which reads vertex
// loads as count = C0(12,8), destination slot = C0(1,7) - count.
#define GEO_OPCODE(w0) ((uint8_t) (((w0) & 0xFF000000) >> 24))
#define GEO_C0(w0, pos, width) (((uint32_t) (w0) >> (pos)) & ((1U << (width)) - 1U))
// A first-word value above this is a cached raw pointer, not an offset (the
// interpreter rewrites the command in place after first execution).
#define GEO_PTR_THRESHOLD 0xFFFFFu

static uint64_t PosKey(const Vtx* v) {
    return ((uint64_t) (uint16_t) v->v.ob[0] << 32) | ((uint64_t) (uint16_t) v->v.ob[1] << 16) |
           (uint64_t) (uint16_t) v->v.ob[2];
}

void GeometryCatalog::Reset() {
    mBuilt = false;
    for (auto& m : mMerged) {
        m.clear();
    }
    mLeafOrder.clear();
    mVisitedLeaves.clear();
    mRefIndex.clear();
    mVertices.clear();
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
    if (path == nullptr) {
        return nullptr;
    }
    return (Gfx*) ResourceGetDataByName(path);
}

void GeometryCatalog::Build(const char* const wrappers[], size_t sectionCount) {
    Reset();

    for (int direction = 0; direction < 4; direction++) {
        std::unordered_set<uint64_t> emittedLeaves;
        for (size_t s = 0; s < sectionCount; s++) {
            Gfx* cmds = ResolveWrapper(wrappers[s * 4 + direction]);
            if (cmds != nullptr) {
                MergeWrapper(cmds, mMerged[direction], emittedLeaves);
            }
        }
        Gfx end;
        end.words.w0 = (uintptr_t) G_ENDDL << 24;
        end.words.w1 = 0;
        mMerged[direction].push_back(end);
    }

    // Vertex enumeration only needs each leaf once; use the north merged list
    // as the canonical leaf set, then add leaves unique to other directions.
    for (int direction = 0; direction < 4; direction++) {
        const std::vector<Gfx>& dl = mMerged[direction];
        for (size_t i = 0; i + 1 < dl.size(); i++) {
            uint8_t op = GEO_OPCODE(dl[i].words.w0);
            if (op == G_DL_OTR_HASH) {
                uint64_t hash = ((uint64_t) dl[i + 1].words.w0 << 32) + dl[i + 1].words.w1;
                Gfx* leaf = (Gfx*) ResourceGetDataByCrc(hash);
                if (leaf != nullptr && mVisitedLeaves.insert(leaf).second) {
                    mLeafOrder.push_back(hash);
                    EnumerateLeaf(leaf, 0);
                }
                i++; // skip hash word
            } else if (op == (uint8_t) G_DL) {
                Gfx* leaf = (Gfx*) dl[i].words.w1;
                if (leaf != nullptr && mVisitedLeaves.insert(leaf).second) {
                    EnumerateLeaf(leaf, 0);
                }
            }
        }
    }

    mBuilt = true;
}

void GeometryCatalog::MergeWrapper(Gfx* cmds, std::vector<Gfx>& out, std::unordered_set<uint64_t>& emittedLeaves) {
    Gfx* ptr = cmds;
    for (int guard = 0; guard < (1 << 16); guard++) {
        uintptr_t w0 = ptr->words.w0;
        uint8_t op = GEO_OPCODE(w0);
        // Branch (no-push) display list calls would let the callee's end
        // command terminate our merged list, so emit them as plain calls.
        bool branch = GEO_C0(w0, 16, 1) != 0;

        if (op == G_ENDDL) {
            return;
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
            }
            ptr += 2;
            if (branch) {
                return;
            }
            continue;
        }
        if (op == (uint8_t) G_DL) {
            uint64_t key = (uint64_t) ptr->words.w1;
            if (emittedLeaves.insert(key).second) {
                Gfx call = ptr[0];
                call.words.w0 &= ~((uintptr_t) 1 << 16);
                out.push_back(call);
            }
            ptr += 1;
            if (branch) {
                return;
            }
            continue;
        }
        if (op == G_DL_OTR_FILEPATH) {
            uint64_t key = (uint64_t) ptr->words.w1;
            if (emittedLeaves.insert(key).second) {
                out.push_back(ptr[0]);
            }
            ptr += 1;
            continue;
        }
        // Any other state command: keep it, order-preserved.
        out.push_back(ptr[0]);
        ptr += 1;
    }
}

void GeometryCatalog::AddRef(const SlotRef& slot, uint32_t offset, Vtx* v) {
    if (slot.baseIndex < 0 || v == nullptr) {
        return;
    }
    std::pair<uint64_t, uint16_t> key(slot.resCrc, (uint16_t) (slot.baseIndex + offset));
    auto it = mRefIndex.find(key);
    if (it != mRefIndex.end()) {
        return;
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

        switch (op) {
            case G_ENDDL:
                return;
            case G_MARKER:
                ptr += 2;
                continue;
            case G_DL_OTR_HASH: {
                uint64_t hash = ((uint64_t) ptr[1].words.w0 << 32) + ptr[1].words.w1;
                EnumerateLeaf((Gfx*) ResourceGetDataByCrc(hash), depth + 1);
                ptr += 2;
                continue;
            }
            case (uint8_t) G_DL:
                if (GEO_C0(w0, 16, 1) != 0) {
                    // Branch: control never returns.
                    EnumerateLeaf((Gfx*) w1, depth + 1);
                    return;
                }
                EnumerateLeaf((Gfx*) w1, depth + 1);
                break;
            case G_DL_OTR_FILEPATH:
                EnumerateLeaf((Gfx*) ResourceGetDataByName((const char*) w1), depth + 1);
                break;
            case G_VTX_OTR_HASH: {
                uint64_t hash = ((uint64_t) ptr[1].words.w0 << 32) + ptr[1].words.w1;
                Vtx* base = (Vtx*) ResourceGetDataByCrc(hash);
                uint32_t count = GEO_C0(w0, 12, 8);
                uint32_t dest = GEO_C0(w0, 1, 7) - count;
                if (base != nullptr) {
                    // The interpreter rewrites w1 from byte offset to raw
                    // pointer after first execution; recover either way.
                    uintptr_t byteOff =
                        (w1 > GEO_PTR_THRESHOLD) ? (uintptr_t) ((char*) w1 - (char*) base) : (uintptr_t) w1;
                    int32_t baseIndex = (int32_t) (byteOff / sizeof(Vtx));
                    AddVertexRange(hash, baseIndex, dest, count, (Vtx*) ((char*) base + byteOff));
                }
                ptr += 2;
                continue;
            }
            case G_VTX_OTR_FILEPATH: {
                // Rare form; render-only (no stable crc identity recorded).
                uint32_t count = (uint32_t) ptr[1].words.w0;
                uint32_t dest = (uint32_t) (ptr[1].words.w1 >> 16);
                AddVertexRange(0, -1, dest, count, nullptr);
                ptr += 2;
                continue;
            }
            case (uint8_t) G_VTX: {
                uint32_t count = GEO_C0(w0, 12, 8);
                uint32_t dest = GEO_C0(w0, 1, 7) - count;
                AddVertexRange(0, -1, dest, count, nullptr);
                break;
            }
            case G_TRI1_OTR: {
                uint32_t v1 = (uint32_t) (w0 & 0xFFFF);
                uint32_t v2 = (uint32_t) (w1 >> 16) & 0xFFFF;
                uint32_t v3 = (uint32_t) (w1 & 0xFFFF);
                if (v1 < 64) {
                    AddRef(mSlots[v1], 0, mSlotData[v1]);
                }
                if (v2 < 64) {
                    AddRef(mSlots[v2], 0, mSlotData[v2]);
                }
                if (v3 < 64) {
                    AddRef(mSlots[v3], 0, mSlotData[v3]);
                }
                break;
            }
            case (uint8_t) G_TRI1: {
                uint32_t v1 = ((uint32_t) (w1 & 0x00FF0000) >> 16) / 2;
                uint32_t v2 = ((uint32_t) (w1 & 0x0000FF00) >> 8) / 2;
                uint32_t v3 = ((uint32_t) (w1 & 0x000000FF)) / 2;
                AddRef(mSlots[v1], 0, mSlotData[v1]);
                AddRef(mSlots[v2], 0, mSlotData[v2]);
                AddRef(mSlots[v3], 0, mSlotData[v3]);
                break;
            }
            case (uint8_t) G_TRI2: {
                uint32_t v1 = ((uint32_t) (w0 & 0x00FF0000) >> 16) / 2;
                uint32_t v2 = ((uint32_t) (w0 & 0x0000FF00) >> 8) / 2;
                uint32_t v3 = ((uint32_t) (w0 & 0x000000FF)) / 2;
                uint32_t v4 = ((uint32_t) (w1 & 0x00FF0000) >> 16) / 2;
                uint32_t v5 = ((uint32_t) (w1 & 0x0000FF00) >> 8) / 2;
                uint32_t v6 = ((uint32_t) (w1 & 0x000000FF)) / 2;
                AddRef(mSlots[v1], 0, mSlotData[v1]);
                AddRef(mSlots[v2], 0, mSlotData[v2]);
                AddRef(mSlots[v3], 0, mSlotData[v3]);
                AddRef(mSlots[v4], 0, mSlotData[v4]);
                AddRef(mSlots[v5], 0, mSlotData[v5]);
                AddRef(mSlots[v6], 0, mSlotData[v6]);
                break;
            }
            default:
                break;
        }
        ptr += 1;
    }
}

} // namespace GeometryEditor
