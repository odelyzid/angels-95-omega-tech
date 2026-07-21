#pragma once
#include <vector>
#include <cstdint>

// CSG brush operation — determines how this brush combines with others
enum class CsgOp : uint8_t {
    SOLID = 0,      // ordinary additive brush (no operation)
    ADD,            // additive volume
    SUB,            // subtractive (carves void from existing solids)
    INTERSECT,      // keeps only volume common to existing solids
    DE_RESC         // same as SUB (remove volume)
};

// A single CSG brush: an AABB with a CSG operation
struct CsgBrush {
    CsgOp op = CsgOp::SOLID;
    float minX = 0, minY = 0, minZ = 0;
    float maxX = 0, maxY = 0, maxZ = 0;
};

// CsgProcessor — applies CSG operations to a set of AABB brushes
// producing a final list of solid collision volumes.
//
// Algorithm (simplified AABB CSG):
//   ADD       → push the AABB as a solid volume
//   SUB/DERESC→ for each existing solid overlapping this brush,
//               split the solid into up to 6 sub-AABBs (the parts
//               outside the subtractor)
//   INTERSECT → for each existing solid overlapping this brush,
//               keep only the overlapping portion
class CsgProcessor {
public:
    void Clear();
    void Apply(const CsgBrush& brush);

    int Count() const { return (int)m_solidMinX.size(); }
    const float* GetSolidMinX() const { return m_solidMinX.data(); }
    const float* GetSolidMinY() const { return m_solidMinY.data(); }
    const float* GetSolidMinZ() const { return m_solidMinZ.data(); }
    const float* GetSolidMaxX() const { return m_solidMaxX.data(); }
    const float* GetSolidMaxY() const { return m_solidMaxY.data(); }
    const float* GetSolidMaxZ() const { return m_solidMaxZ.data(); }

    // Convenience: fill a vector of BoundingBox-like structs
    struct Volume { float minX, minY, minZ, maxX, maxY, maxZ; };
    void GetVolumes(std::vector<Volume>& out) const;

private:
    struct AABB { float minX, minY, minZ, maxX, maxY, maxZ; };

    std::vector<float> m_solidMinX, m_solidMinY, m_solidMinZ;
    std::vector<float> m_solidMaxX, m_solidMaxY, m_solidMaxZ;

    bool Overlaps(int idx, const AABB& brush) const;
    void RemoveAt(int idx);
    void Push(const AABB& aabb);
    void Subtract(int idx, const AABB& sub);
    void Intersect(int idx, const AABB& isect);
};
