#pragma once

#include "clientcore/meshing/QcBlockRenderTable.h"
#include "clientcore/meshing/QcChunkMesher.h"
#include "common/core/QcWorldConstants.h"

#include <optional>

// The blocks of the 3 x 3 chunks around a section as the chunk mesher reads them (QcMeshInput),
// by chunk-local coordinates.
class QcMeshNeighbourhood
{
public:
    explicit QcMeshNeighbourhood(const QcMeshInput &input)
        : m_input(input)
    {
    }

    // Block at chunk-local coordinates; x and z may lie in [-16, 32).
    // Returns nullopt where the neighbour chunk is missing. Below and above the world is air, so
    // the underside of the bottom layer is drawn for a view from the void (decision E-288).
    std::optional<QcBlockId> block(int x, int y, int z) const
    {
        if (y < 0 || y >= QcWorld::worldHeight) {
            return qcAirBlockId;
        }
        const int cx = x < 0 ? -1 : (x >= QcWorld::chunkSize ? 1 : 0);
        const int cz = z < 0 ? -1 : (z >= QcWorld::chunkSize ? 1 : 0);
        const int index = (cz + 1) * 3 + (cx + 1);
        const auto &chunk = m_input.chunks[static_cast<std::size_t>(index)];
        if (!chunk) {
            return std::nullopt;
        }
        return chunk->block(x - cx * QcWorld::chunkSize, y, z - cz * QcWorld::chunkSize);
    }

    bool isOpaque(int x, int y, int z) const
    {
        const std::optional<QcBlockId> id = block(x, y, z);
        return !id || m_input.renderTable->entry(*id).opaque;
    }

private:
    const QcMeshInput &m_input;
};
