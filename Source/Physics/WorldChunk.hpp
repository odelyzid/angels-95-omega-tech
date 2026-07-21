#pragma once
#include <vector>
#include <unordered_map>
#include <cstdint>

// WorldChunkManager — uniform grid spatial partitioning for OZONE collision volumes.
//
// Divides the world into CHUNK_SIZE × CHUNK_SIZE cells. Each cell stores
// indices into a flat volume array. Lookup is O(1) — player's cell + 8
// adjacent cells are queried each frame instead of scanning all volumes.
class WorldChunkManager {
public:
    struct Volume {
        float minX, minY, minZ, maxX, maxY, maxZ;
    };

    void Clear();

    // Build grid from a set of volumes. chunkSize = world units per cell side.
    void Build(const std::vector<Volume>& volumes, float chunkSize = 32.0f,
               float worldMinX = -5000.0f, float worldMaxX = 5000.0f,
               float worldMinZ = -5000.0f, float worldMaxZ = 5000.0f);

    // Get volumes in the cell at the given world position.
    void GetVolumesInCell(float worldX, float worldZ, std::vector<int>& outIndices) const;

    // Get volumes in the cell containing (worldX, worldZ) and `range` cells
    // in each direction (default 1 = 3x3 cell neighborhood).
    void GetVolumesNear(float worldX, float worldZ, std::vector<int>& outIndices, int range = 1) const;

    // Direct access to the flat volume array (for server serialization, etc.)
    const std::vector<Volume>& GetAllVolumes() const { return m_volumes; }

    int VolumeCount() const { return (int)m_volumes.size(); }
    int CellCount() const { return (int)m_cells.size(); }

private:
    int CellIndex(float worldX, float worldZ) const;

    float m_chunkSize = 32.0f;
    float m_worldMinX = -5000.0f, m_worldMaxX = 5000.0f;
    float m_worldMinZ = -5000.0f, m_worldMaxZ = 5000.0f;
    int m_cellsX = 0, m_cellsZ = 0;

    std::vector<Volume> m_volumes;
    std::unordered_map<int, std::vector<int>> m_cells;
};
