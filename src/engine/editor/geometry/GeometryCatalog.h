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
    float nrm[3] = { 0, 0, 0 }; // accumulated face normal (unnormalized)
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
    // True when a build attempt found data it could not safely parse; the
    // mode falls back to stock rendering instead of guessing.
    bool Failed() const {
        return mFailed;
    }
    void Build(const char* const wrappers[], size_t sectionCount);

    const std::vector<Gfx>& MergedDL(int direction) const {
        return mMerged[direction & 3];
    }
    const std::vector<VertexRef>& Vertices() const {
        return mVertices;
    }
    // Unique triangle edges as index pairs into Vertices(), for wireframe
    // context in the overlay.
    const std::vector<std::pair<uint32_t, uint32_t>>& Edges() const {
        return mEdges;
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
    bool MergeWrapper(Gfx* cmds, std::vector<Gfx>& out, std::unordered_set<uint64_t>& emittedLeaves);
    void EmitLeafStateOnly(Gfx* cmds, std::vector<Gfx>& out, int depth);
    void EnumerateLeaf(Gfx* cmds, int depth);
    void AddVertexRange(uint64_t crc, int32_t baseIndex, uint32_t dest, uint32_t count, Vtx* data);
    int32_t AddRef(const SlotRef& slot, uint32_t offset, Vtx* v);
    void AddTriangleEdges(int32_t a, int32_t b, int32_t c);

    bool mBuilt = false;
    bool mFailed = false;
    std::vector<Gfx> mMerged[4];
    std::vector<uint64_t> mLeafOrder;
    std::unordered_set<uint64_t> mLeafSeen;
    SlotRef mSlots[64];
    Vtx* mSlotData[64] = { nullptr };
    std::map<std::pair<uint64_t, uint16_t>, size_t> mRefIndex;
    std::vector<VertexRef> mVertices;
    std::vector<std::pair<uint32_t, uint32_t>> mEdges;
    std::unordered_set<uint64_t> mEdgeSeen;
    std::unordered_map<uint64_t, uint32_t> mWeldByPos;
    uint32_t mWeldGroups = 0;
};

} // namespace GeometryEditor
