// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/nether/QcNetherDeltas.h"

#include "worldgen/nether/QcNetherTerrain.h"

#include <algorithm>
#include <array>

namespace {

// The rock bands: how thick, how wavy across the land, and the share of the other stone.
constexpr int bandHeight = 5;
constexpr double bandWave = 9.0;
constexpr double bandScale = 44.0;
constexpr double bandShare = 0.35;
// Posts stand in squares of this width, and one strength of the noise gives their full height.
constexpr int postWidth = 2;
constexpr double fullStrength = 0.3;

constexpr std::array<std::pair<int, int>, 4> sides{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

// The grid square of a post of `width` a coordinate lies in, also below zero.
int squareOf(int value, int width)
{
    return value >= 0 ? value / width : (value + 1) / width - 1;
}

} // namespace

QcNetherDeltas::QcNetherDeltas(const QcNetherDefinitions &definitions, const QcNetherTerrain &terrain)
    : m_definitions(definitions)
    , m_terrain(terrain)
{
}

bool QcNetherDeltas::holdsLiquid(const QcChunk &chunk, QcBlockPos local) const
{
    if (local.x >= 0 && local.x < QcWorld::chunkSize && local.z >= 0 && local.z < QcWorld::chunkSize) {
        return chunk.block(local.x, local.y, local.z) != qcAirBlockId;
    }
    return m_terrain.isSolid(chunk.pos().x * QcWorld::chunkSize + local.x, local.y, chunk.pos().z * QcWorld::chunkSize + local.z);
}

void QcNetherDeltas::rockColumn(QcChunk &chunk, const QcNetherDefinitions::Biome &biome, int localX, int localZ) const
{
    const int worldX = chunk.pos().x * QcWorld::chunkSize + localX;
    const int worldZ = chunk.pos().z * QcWorld::chunkSize + localZ;
    const int wave = static_cast<int>(bandWave * (1.0 + m_definitions.bandNoise.fractal2(worldX / bandScale, worldZ / bandScale, 2)));
    for (int y = 1; y < m_definitions.settings.roof; ++y) {
        if (chunk.block(localX, y, localZ) == m_definitions.filler) {
            const bool band = qcPositionRandom(m_definitions.patchSeed, 0, (y + wave) / bandHeight, 0) < bandShare;
            chunk.setBlock(localX, y, localZ, band ? biome.rockBand : biome.rock);
        }
    }
}

void QcNetherDeltas::poolColumn(QcChunk &chunk, const QcNetherDefinitions::Pool &pool, int localX, int localZ) const
{
    const int worldX = chunk.pos().x * QcWorld::chunkSize + localX;
    const int worldZ = chunk.pos().z * QcWorld::chunkSize + localZ;
    const double value = pool.noise.fractal2(worldX / pool.scale, worldZ / pool.scale, 2);
    if (value <= pool.threshold - pool.rimBand) {
        return;
    }

    const int level = m_definitions.settings.liquidLevel;
    for (int y = m_definitions.settings.roof - 1; y > level + 1; --y) {
        const bool floor = chunk.block(localX, y, localZ) != qcAirBlockId && chunk.block(localX, y + 1, localZ) == qcAirBlockId;
        if (!floor) {
            continue;
        }
        const QcBlockPos cell{localX, y, localZ};
        const bool walled = std::ranges::all_of(sides, [&](const auto &side) { return holdsLiquid(chunk, cell.offset(side.first, 0, side.second)); });
        const bool held = chunk.block(localX, y - 1, localZ) != qcAirBlockId;
        chunk.setBlock(localX, y, localZ, value > pool.threshold && walled && held ? pool.block : pool.rim);
    }
}

void QcNetherDeltas::pillarColumn(QcChunk &chunk, const QcNetherDefinitions::Pillar &pillar, int localX, int localZ) const
{
    const int worldX = chunk.pos().x * QcWorld::chunkSize + localX;
    const int worldZ = chunk.pos().z * QcWorld::chunkSize + localZ;
    const double strength = pillar.noise.fractal2(worldX / pillar.scale, worldZ / pillar.scale, 2) - pillar.threshold;
    if (strength <= 0.0) {
        return;
    }

    // One height for a square of columns, a step more or less for each.
    const double post = qcPositionRandom(pillar.seed, squareOf(worldX, postWidth), 0, squareOf(worldZ, postWidth));
    const double step = qcPositionRandom(pillar.seed, worldX, 1, worldZ);
    const int height = static_cast<int>(pillar.height * std::min(1.0, strength / fullStrength) * post * post + 2.0 * step);
    if (height <= 0) {
        return;
    }

    const int level = m_definitions.settings.liquidLevel;
    const int roof = m_definitions.settings.roof;
    for (int y = roof - 2; y > level + 1; --y) {
        if (chunk.block(localX, y, localZ) != qcAirBlockId) {
            continue;
        }
        const bool onFloor = chunk.block(localX, y - 1, localZ) != qcAirBlockId && chunk.block(localX, y - 1, localZ) != m_definitions.liquid;
        const bool underRoof = chunk.block(localX, y + 1, localZ) != qcAirBlockId;
        if (onFloor) {
            for (int cell = 0; cell < height && y + cell < roof - 1 && chunk.block(localX, y + cell, localZ) == qcAirBlockId; ++cell) {
                chunk.setBlock(localX, y + cell, localZ, pillar.block);
            }
        } else if (underRoof) {
            for (int cell = 0; cell < height / 2 && y - cell > level && chunk.block(localX, y - cell, localZ) == qcAirBlockId; ++cell) {
                chunk.setBlock(localX, y - cell, localZ, pillar.block);
            }
        }
    }
}

void QcNetherDeltas::shapeColumn(QcChunk &chunk, int localX, int localZ) const
{
    const int worldX = chunk.pos().x * QcWorld::chunkSize + localX;
    const int worldZ = chunk.pos().z * QcWorld::chunkSize + localZ;
    const QcNetherDefinitions::Biome &biome = m_definitions.biomes[m_terrain.biomeAt(worldX, worldZ)];
    if (biome.rock != qcAirBlockId) {
        rockColumn(chunk, biome, localX, localZ);
    }
    for (const QcNetherDefinitions::Pool &pool : m_definitions.pools) {
        if (pool.biomes.contains(biome.name)) {
            poolColumn(chunk, pool, localX, localZ);
        }
    }
    for (const QcNetherDefinitions::Pillar &pillar : m_definitions.pillars) {
        if (pillar.biomes.contains(biome.name)) {
            pillarColumn(chunk, pillar, localX, localZ);
        }
    }
}

void QcNetherDeltas::shape(QcChunk &chunk) const
{
    for (int z = 0; z < QcWorld::chunkSize; ++z) {
        for (int x = 0; x < QcWorld::chunkSize; ++x) {
            shapeColumn(chunk, x, z);
        }
    }
}
