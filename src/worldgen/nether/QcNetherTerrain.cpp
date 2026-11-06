// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/nether/QcNetherTerrain.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr int cornersX = QcWorld::chunkSize / QcNetherTerrain::cellWidth + 1;
constexpr int cornersY = QcWorld::worldHeight / QcNetherTerrain::cellHeight + 1;

constexpr std::size_t cornerIndex(int x, int y, int z)
{
    const int index = x + cornersX * (z + cornersX * y);
    return static_cast<std::size_t>(index);
}

// The grid cell below `value` and how far into it `value` lies, in 0 to 1.
struct GridPoint
{
    int cell = 0;
    double part = 0.0;
};

GridPoint gridPoint(int value, int size)
{
    const int cell = static_cast<int>(std::floor(static_cast<double>(value) / size));
    return {cell, static_cast<double>(value - cell * size) / size};
}

double mix(double from, double to, double part)
{
    return from + (to - from) * part;
}

// The value between the eight corners around a cell: `at(dx, dy, dz)` gives each corner.
template <typename Corner>
double trilinear(const Corner &at, double partX, double partY, double partZ)
{
    const double low = mix(mix(at(0, 0, 0), at(1, 0, 0), partX), mix(at(0, 0, 1), at(1, 0, 1), partX), partZ);
    const double high = mix(mix(at(0, 1, 0), at(1, 1, 0), partX), mix(at(0, 1, 1), at(1, 1, 1), partX), partZ);
    return mix(low, high, partY);
}

} // namespace

QcNetherTerrain::QcNetherTerrain(const QcNetherDefinitions &definitions, quint64 seed)
    : m_definitions(definitions)
    , m_bedrockSeed(qcMixSeed(seed, 11))
    , m_noise(qcMixSeed(seed, 12))
    , m_biomeFirst(qcMixSeed(seed, 13))
    , m_biomeSecond(qcMixSeed(seed, 14))
{
}

double QcNetherTerrain::density(int x, int y, int z) const
{
    const QcNetherSettings::Shape &shape = m_definitions.settings.shape;
    const double noise = m_noise.fractal3(x / shape.scale, y / shape.verticalScale, z / shape.scale, shape.octaves);
    const double floorPull = std::max(0.0, static_cast<double>(shape.floorHeight - y) / shape.floorFade);
    const double roofPull = std::max(0.0, static_cast<double>(y - shape.roofHeight) / shape.roofFade);
    return noise + shape.solidity + floorPull + roofPull;
}

bool QcNetherTerrain::isBedrock(int x, int y, int z) const
{
    const QcNetherSettings &settings = m_definitions.settings;
    const int depth = std::min(y, settings.roof - y);
    if (depth >= settings.bedrockLayers) {
        return false;
    }
    return depth <= 0 || qcPositionRandom(m_bedrockSeed, x, y, z) < 1.0 - static_cast<double>(depth) / settings.bedrockLayers;
}

bool QcNetherTerrain::isSolid(int x, int y, int z) const
{
    if (y < 0 || y > m_definitions.settings.roof) {
        return false;
    }
    if (isBedrock(x, y, z)) {
        return true;
    }
    const GridPoint gridX = gridPoint(x, cellWidth);
    const GridPoint gridY = gridPoint(y, cellHeight);
    const GridPoint gridZ = gridPoint(z, cellWidth);
    const auto corner = [&](int dx, int dy, int dz) {
        return density((gridX.cell + dx) * cellWidth, (gridY.cell + dy) * cellHeight, (gridZ.cell + dz) * cellWidth);
    };
    return trilinear(corner, gridX.part, gridY.part, gridZ.part) > 0.0;
}

void QcNetherTerrain::fillColumn(QcChunk &chunk, const Corners &corners, int localX, int localZ) const
{
    const QcNetherSettings &settings = m_definitions.settings;
    const int worldX = chunk.pos().x * QcWorld::chunkSize + localX;
    const int worldZ = chunk.pos().z * QcWorld::chunkSize + localZ;
    const GridPoint gridX = gridPoint(localX, cellWidth);
    const GridPoint gridZ = gridPoint(localZ, cellWidth);
    for (int y = 0; y <= settings.roof; ++y) {
        const GridPoint gridY = gridPoint(y, cellHeight);
        const auto corner = [&](int dx, int dy, int dz) { return corners[cornerIndex(gridX.cell + dx, gridY.cell + dy, gridZ.cell + dz)]; };
        if (isBedrock(worldX, y, worldZ)) {
            chunk.setBlock(localX, y, localZ, m_definitions.bedrock);
        } else if (trilinear(corner, gridX.part, gridY.part, gridZ.part) > 0.0) {
            chunk.setBlock(localX, y, localZ, m_definitions.filler);
        } else if (y <= settings.liquidLevel) {
            chunk.setBlock(localX, y, localZ, m_definitions.liquid);
        }
    }
}

void QcNetherTerrain::fill(QcChunk &chunk) const
{
    static_assert(static_cast<std::size_t>(cornersX) * cornersX * cornersY == std::tuple_size_v<Corners>);
    Corners corners{};
    const int originX = chunk.pos().x * QcWorld::chunkSize;
    const int originZ = chunk.pos().z * QcWorld::chunkSize;
    const int rows = m_definitions.settings.roof / cellHeight + 2;
    for (int y = 0; y < std::min(rows, cornersY); ++y) {
        for (int z = 0; z < cornersX; ++z) {
            for (int x = 0; x < cornersX; ++x) {
                corners[cornerIndex(x, y, z)] = density(originX + x * cellWidth, y * cellHeight, originZ + z * cellWidth);
            }
        }
    }
    for (int z = 0; z < QcWorld::chunkSize; ++z) {
        for (int x = 0; x < QcWorld::chunkSize; ++x) {
            fillColumn(chunk, corners, x, z);
        }
    }
}

int QcNetherTerrain::biomeAt(int x, int z) const
{
    const double scale = m_definitions.settings.biomeScale;
    const double first = 0.5 + 0.8 * m_biomeFirst.fractal2(x / scale, z / scale, 3);
    const double second = 0.5 + 0.8 * m_biomeSecond.fractal2(x / scale, z / scale, 3);

    int nearest = 0;
    double nearestDistance = std::numeric_limits<double>::max();
    for (qsizetype i = 0; i < m_definitions.biomes.size(); ++i) {
        const QcNetherDefinitions::Biome &biome = m_definitions.biomes[i];
        const double distance = (first - biome.first) * (first - biome.first) + (second - biome.second) * (second - biome.second);
        if (distance < nearestDistance) {
            nearestDistance = distance;
            nearest = static_cast<int>(i);
        }
    }
    return nearest;
}

std::optional<int> QcNetherTerrain::floorAt(int x, int z, int from, int room) const
{
    const QcNetherSettings &settings = m_definitions.settings;
    int free = 0;
    for (int y = std::max(from, settings.liquidLevel + 1); y + 1 < settings.roof; ++y) {
        if (isSolid(x, y, z)) {
            free = 0;
            continue;
        }
        if (free == 0 && !isSolid(x, y - 1, z)) {
            continue;
        }
        if (++free >= room) {
            return y - room;
        }
    }
    return std::nullopt;
}
