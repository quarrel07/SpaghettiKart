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
    bool drawnInRace = false; // also referenced by the racing section lists
};

// A course section's physical location, from the TrackSections asset.
struct SectionCentroid {
    int sectionId;
    float pos[3];
};

// One racing leaf's LOD classification.
enum class LeafClass : uint8_t {
    Near,     // its home section's own lists draw it: the up-close version
    Far,      // only distant sections draw it and a Near leaf overlaps it
    Unique,   // no overlapping sibling; kept in both Force modes
    Unplaced, // no vertex data to place it with; kept in both Force modes
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
    // creditsRoot, when non-null, is the authored full-course credits list;
    // vertices/edges are enumerated from it so the catalog matches what the
    // stable credits assembly actually draws. Without it, enumeration falls
    // back to the merged union.
    void Build(const char* const wrappers[], size_t sectionCount, Gfx* creditsRoot,
               const std::vector<SectionCentroid>& sectionCentroids);

    struct LeafInfo {
        uint64_t hash = 0;
        float boxMin[3] = { 0, 0, 0 };
        float boxMax[3] = { 0, 0, 0 };
        bool hasBox = false;
        int homeSection = -1;
        bool inHomeList = false;
        bool hasFarSibling = false; // a Far leaf substantially overlaps this Near leaf
        LeafClass cls = LeafClass::Unplaced;
    };
    const std::vector<LeafInfo>& Leaves() const {
        return mLeafInfo;
    }

    const std::vector<Gfx>& MergedDL(int direction) const {
        return mMerged[direction & 3];
    }
    // Force No LOD: near + unique + unplaced. Force LOD: far + unique + unplaced.
    const std::vector<Gfx>& NearDL(int direction) const {
        return mNearMerged[direction & 3];
    }
    const std::vector<Gfx>& FarDL(int direction) const {
        return mFarMerged[direction & 3];
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
    bool mMarkOnly = false; // enumeration pass flags existing refs instead of creating
    bool MergeWrapper(Gfx* cmds, std::vector<Gfx>& out, std::unordered_set<uint64_t>& emittedLeaves, int lodFilter);
    void ClassifyLeaves(const std::vector<SectionCentroid>& sectionCentroids);
    void EmitLeafStateOnly(Gfx* cmds, std::vector<Gfx>& out, int depth);
    void EnumerateLeaf(Gfx* cmds, int depth);
    void AddVertexRange(uint64_t crc, int32_t baseIndex, uint32_t dest, uint32_t count, Vtx* data);
    int32_t AddRef(const SlotRef& slot, uint32_t offset, Vtx* v);
    void AddTriangleEdges(int32_t a, int32_t b, int32_t c);

    bool mBuilt = false;
    bool mFailed = false;
    std::vector<Gfx> mMerged[4];
    std::vector<Gfx> mNearMerged[4];
    std::vector<Gfx> mFarMerged[4];
    std::vector<uint64_t> mLeafOrder;
    std::unordered_set<uint64_t> mLeafSeen;
    std::unordered_map<uint64_t, std::unordered_set<int>> mLeafSections; // leaf -> section ids listing it
    std::vector<LeafInfo> mLeafInfo;
    std::unordered_map<uint64_t, size_t> mLeafInfoIndex;
    uint64_t mCurrentLeaf = 0; // attribution target during enumeration
    int mMergeSection = -1;    // section id during wrapper merging
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
