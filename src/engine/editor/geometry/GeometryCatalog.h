#pragma once

#include <cstdint>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <libultraship/libultraship.h>
#include <libultra/gbi.h>

namespace GeometryEditor {

// One course vertex the renderer actually references, identified the same
// way enhancement fixes address it: vertex resource + index into that blob.
struct VertexRef {
    uint64_t resCrc;
    uint16_t index;
    int16_t pos[3]; // position at catalog build (pre-edit baseline)
    uint32_t weldGroup;
    Vtx* live; // resolved pointer into the loaded resource (process-cached)
};

// Built once per course from the primary visibility table: the deduplicated
// leaf set, four merged per-direction display lists, and every referenced
// vertex with weld grouping by identical position.
class GeometryCatalog {
  public:
    void Reset();
    bool Built() const {
        return mBuilt;
    }
    void Build(const char* const wrappers[], size_t sectionCount);

    const std::vector<Gfx>& MergedDL(int direction) const {
        return mMerged[direction & 3];
    }
    const std::vector<VertexRef>& Vertices() const {
        return mVertices;
    }
    size_t LeafCount() const {
        return mLeafOrder.size();
    }
    size_t WeldGroupCount() const {
        return mWeldGroups;
    }

  private:
    struct SlotRef {
        uint64_t resCrc = 0;
        int32_t baseIndex = -1; // -1 = unattributed (raw pointer load)
    };

    Gfx* ResolveWrapper(const char* path);
    void MergeWrapper(Gfx* cmds, std::vector<Gfx>& out, std::unordered_set<uint64_t>& emittedLeaves);
    void EnumerateLeaf(Gfx* cmds, int depth);
    void AddVertexRange(uint64_t crc, int32_t baseIndex, uint32_t dest, uint32_t count, Vtx* data);
    void AddRef(const SlotRef& slot, uint32_t offset, Vtx* v);

    bool mBuilt = false;
    std::vector<Gfx> mMerged[4];
    std::vector<uint64_t> mLeafOrder;
    std::unordered_set<const void*> mVisitedLeaves;
    SlotRef mSlots[64];
    Vtx* mSlotData[64] = { nullptr };
    std::map<std::pair<uint64_t, uint16_t>, size_t> mRefIndex;
    std::vector<VertexRef> mVertices;
    std::unordered_map<uint64_t, uint32_t> mWeldByPos;
    uint32_t mWeldGroups = 0;
};

} // namespace GeometryEditor
