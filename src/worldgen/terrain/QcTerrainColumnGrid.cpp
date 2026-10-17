#include "worldgen/terrain/QcTerrainColumnGrid.h"

#include "worldgen/terrain/QcTerrainGenerator.h"

#include <algorithm>
#include <cstdlib>

QcTerrainColumnGrid::QcTerrainColumnGrid(const QcTerrainGenerator &generator, QcChunkPos pos)
    : m_baseX(pos.x * QcWorld::chunkSize - margin)
    , m_baseZ(pos.z * QcWorld::chunkSize - margin)
{
    m_columns.reserve(qsizetype(size) * size);
    for (int z = 0; z < size; ++z) {
        for (int x = 0; x < size; ++x) {
            m_columns.append(generator.column(m_baseX + x, m_baseZ + z));
        }
    }

    // Slopes inside the outermost ring; the ring itself is only a neighbour.
    for (int z = 1; z < size - 1; ++z) {
        for (int x = 1; x < size - 1; ++x) {
            QcTerrainColumn &column = m_columns[z * size + x];
            const int height = column.height;
            column.slope = std::max({std::abs(m_columns[z * size + x - 1].height - height),
                                     std::abs(m_columns[z * size + x + 1].height - height),
                                     std::abs(m_columns[(z - 1) * size + x].height - height),
                                     std::abs(m_columns[(z + 1) * size + x].height - height)});
        }
    }
}
