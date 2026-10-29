// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/terrain/QcTerrainColumnGrid.h"

#include "worldgen/terrain/QcTerrainGenerator.h"

#include <algorithm>
#include <cstdlib>

QcTerrainColumnGrid::QcTerrainColumnGrid(const QcTerrainGenerator &generator, QcChunkPos pos)
    : m_baseX(pos.x * QcWorld::chunkSize - margin)
    , m_baseZ(pos.z * QcWorld::chunkSize - margin)
{
    m_columns.reserve(static_cast<qsizetype>(size) * size);
    for (int z = 0; z < size; ++z) {
        for (int x = 0; x < size; ++x) {
            m_columns.append(generator.column(m_baseX + x, m_baseZ + z));
        }
    }
    lookUpGround(generator);

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

void QcTerrainColumnGrid::lookUpGround(const QcTerrainGenerator &generator)
{
    // The ground source lies a few columns away: inside the grid its land biome is known already,
    // and only a column of the chunk itself evaluates the climate outside it. The margin's columns
    // stay unresolved then; a feature on one looks its ground up (QcTerrainGenerator::withGround()).
    for (int z = 0; z < size; ++z) {
        for (int x = 0; x < size; ++x) {
            QcTerrainColumn &column = m_columns[z * size + x];
            const int worldX = m_baseX + x;
            const int worldZ = m_baseZ + z;
            const std::optional<std::pair<int, int>> source = generator.groundSource(worldX, worldZ, column);
            if (!source) {
                continue;
            }
            const auto [sourceX, sourceZ] = *source;
            const bool inChunk = x >= margin && x < size - margin && z >= margin && z < size - margin;
            if (contains(sourceX, sourceZ)) {
                column.groundBiome = generator.groundBiomeFor(column, at(sourceX, sourceZ).landBiome);
            } else if (inChunk) {
                column.groundBiome = generator.groundBiomeFor(column, generator.landBiomeAt(sourceX, sourceZ));
            }
        }
    }
}
