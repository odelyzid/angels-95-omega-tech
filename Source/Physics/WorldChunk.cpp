#include "WorldChunk.hpp"
#include <algorithm>
#include <cmath>

void WorldChunkManager::Clear() {
    m_volumes.clear();
    m_cells.clear();
}

void WorldChunkManager::Build(const std::vector<Volume>& volumes, float chunkSize,
                              float worldMinX, float worldMaxX,
                              float worldMinZ, float worldMaxZ) {
    Clear();
    m_chunkSize = chunkSize;
    m_worldMinX = worldMinX;
    m_worldMaxX = worldMaxX;
    m_worldMinZ = worldMinZ;
    m_worldMaxZ = worldMaxZ;
    m_cellsX = (int)std::ceil((worldMaxX - worldMinX) / chunkSize);
    m_cellsZ = (int)std::ceil((worldMaxZ - worldMinZ) / chunkSize);
    if (m_cellsX < 1) m_cellsX = 1;
    if (m_cellsZ < 1) m_cellsZ = 1;

    m_volumes = volumes;

    // Assign each volume to all cells it overlaps
    for (int vi = 0; vi < (int)m_volumes.size(); vi++) {
        auto& v = m_volumes[vi];
        // Clamp volume bounds to world bounds to avoid out-of-range cells
        float cminX = std::max(v.minX, m_worldMinX);
        float cmaxX = std::min(v.maxX, m_worldMaxX);
        float cminZ = std::max(v.minZ, m_worldMinZ);
        float cmaxZ = std::min(v.maxZ, m_worldMaxZ);

        int ix0 = (int)std::floor((cminX - m_worldMinX) / m_chunkSize);
        int ix1 = (int)std::floor((cmaxX - m_worldMinX) / m_chunkSize);
        int iz0 = (int)std::floor((cminZ - m_worldMinZ) / m_chunkSize);
        int iz1 = (int)std::floor((cmaxZ - m_worldMinZ) / m_chunkSize);
        if (ix0 < 0) ix0 = 0; if (ix1 >= m_cellsX) ix1 = m_cellsX - 1;
        if (iz0 < 0) iz0 = 0; if (iz1 >= m_cellsZ) iz1 = m_cellsZ - 1;

        for (int iz = iz0; iz <= iz1; iz++) {
            for (int ix = ix0; ix <= ix1; ix++) {
                int idx = iz * m_cellsX + ix;
                m_cells[idx].push_back(vi);
            }
        }
    }
}

int WorldChunkManager::CellIndex(float worldX, float worldZ) const {
    int ix = (int)std::floor((worldX - m_worldMinX) / m_chunkSize);
    int iz = (int)std::floor((worldZ - m_worldMinZ) / m_chunkSize);
    if (ix < 0) ix = 0; if (ix >= m_cellsX) ix = m_cellsX - 1;
    if (iz < 0) iz = 0; if (iz >= m_cellsZ) iz = m_cellsZ - 1;
    return iz * m_cellsX + ix;
}

void WorldChunkManager::GetVolumesInCell(float worldX, float worldZ,
                                          std::vector<int>& outIndices) const {
    outIndices.clear();
    int idx = CellIndex(worldX, worldZ);
    auto it = m_cells.find(idx);
    if (it != m_cells.end())
        outIndices = it->second;
}

void WorldChunkManager::GetVolumesNear(float worldX, float worldZ,
                                        std::vector<int>& outIndices, int range) const {
    outIndices.clear();
    int cx = (int)std::floor((worldX - m_worldMinX) / m_chunkSize);
    int cz = (int)std::floor((worldZ - m_worldMinZ) / m_chunkSize);

    for (int dz = -range; dz <= range; dz++) {
        for (int dx = -range; dx <= range; dx++) {
            int ix = cx + dx;
            int iz = cz + dz;
            if (ix < 0 || ix >= m_cellsX || iz < 0 || iz >= m_cellsZ) continue;
            int idx = iz * m_cellsX + ix;
            auto it = m_cells.find(idx);
            if (it != m_cells.end()) {
                for (int vi : it->second) {
                    // Avoid duplicates if a volume spans multiple cells
                    if (std::find(outIndices.begin(), outIndices.end(), vi) == outIndices.end())
                        outIndices.push_back(vi);
                }
            }
        }
    }
}
