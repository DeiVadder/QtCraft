// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/generators/QcTestTerrainGenerator.h"

#include <cmath>

using namespace Qt::StringLiterals;

QcTestTerrainGenerator::QcTestTerrainGenerator(std::shared_ptr<const QcContentRegistry> content)
    : m_content(std::move(content))
{
    m_stone = m_content->blockId(u"qtcraft:stone"_s).value_or(qcAirBlockId);
    m_dirt = m_content->blockId(u"qtcraft:dirt"_s).value_or(m_stone);
    m_grass = m_content->blockId(u"qtcraft:grass_block"_s).value_or(m_dirt);
    m_sand = m_content->blockId(u"qtcraft:sand"_s).value_or(m_dirt);
    m_water = m_content->blockId(u"qtcraft:water"_s).value_or(qcAirBlockId);
}

int QcTestTerrainGenerator::surfaceHeight(int x, int z)
{
    const double hills = 4.0 * std::sin(x * 0.11) + 3.0 * std::cos(z * 0.09) + 2.0 * std::sin((x + z) * 0.05);
    return QcWorld::seaLevel + 3 + static_cast<int>(std::floor(hills));
}

QcBlockId QcTestTerrainGenerator::groundBlock(int y, int height) const
{
    if (y >= height - 1) {
        return height <= QcWorld::seaLevel + 1 ? m_sand : m_grass;
    }

    if (y >= height - 4) {
        return height <= QcWorld::seaLevel + 1 ? m_sand : m_dirt;
    }
    return m_stone;
}

std::shared_ptr<QcChunk> QcTestTerrainGenerator::generate(QcChunkPos pos) const
{
    auto chunk = std::make_shared<QcChunk>(pos);
    for (int lx = 0; lx < QcWorld::chunkSize; ++lx) {
        for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
            const int x = pos.x * QcWorld::chunkSize + lx;
            const int z = pos.z * QcWorld::chunkSize + lz;
            const int height = surfaceHeight(x, z);

            for (int y = 0; y < height; ++y) {
                chunk->setBlock(lx, y, lz, groundBlock(y, height));
            }

            for (int y = height; y < QcWorld::seaLevel; ++y) {
                chunk->setBlock(lx, y, lz, m_water);
            }
        }
    }
    return chunk;
}

QcBlockPos QcTestTerrainGenerator::spawnPoint() const
{
    return {0, std::max(surfaceHeight(0, 0), QcWorld::seaLevel), 0};
}
