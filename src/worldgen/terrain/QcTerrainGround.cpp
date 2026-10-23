#include "worldgen/terrain/QcTerrainGenerator.h"

#include <algorithm>
#include <cstdlib>

// The ground and the ice of a column at the edges of cold biomes (decision E-858): which biome's
// ground it takes and whether its water freezes.

std::optional<std::pair<int, int>> QcTerrainGenerator::groundSource(int x, int z, const Column &column) const
{
    if (column.groundBiome != Column::unresolvedGround) {
        return std::nullopt;
    }
    const auto [dx, dz] = m_coldEdges.groundOffset(x, z);
    return std::pair{x + dx, z + dz};
}

int QcTerrainGenerator::groundBiomeFor(const Column &column, int land) const
{
    if (land == column.landBiome) {
        return -1;
    }
    // A river channel takes the bed of the other land's river; a land without one keeps its own.
    if (isRiverBiome(column.biome)) {
        const int river = m_definitions.biomes[land].riverBiome;
        return river >= 0 && river != column.biome ? river : -1;
    }
    return land;
}

int QcTerrainGenerator::landBiomeAt(int x, int z) const
{
    return biomeForClimate(climateAt(x, z));
}

QcTerrainGenerator::Column QcTerrainGenerator::withGround(int x, int z, Column column) const
{
    if (const std::optional<std::pair<int, int>> source = groundSource(x, z, column)) {
        const auto [sourceX, sourceZ] = *source;
        column.groundBiome = sourceX == x && sourceZ == z ? -1 : groundBiomeFor(column, landBiomeAt(sourceX, sourceZ));
    }
    return column;
}

QcBlockId QcTerrainGenerator::frozenWaterAt(int x, int z, const Column &column) const
{
    const Biome &biome = m_definitions.biomes[column.biome];
    // With a `frozenDepth` only where the water is that shallow (ice along the banks of a cold
    // delta, decision E-850).
    if (biome.frozenDepth > 0 && seaLevel() - column.height > biome.frozenDepth) {
        return m_definitions.water;
    }
    if (!m_coldEdges.byTemperature()) {
        return biome.frozenWater;
    }
    return m_coldEdges.freezes(column.temperature, isRiverBiome(column.biome), x, z) ? biome.frozenWater : m_definitions.water;
}

QcBlockId QcTerrainGenerator::surfaceBlockAt(int x, int z) const
{
    Column info = withGround(x, z, column(x, z));
    if (m_definitions.biomes[info.ground()].steep != qcAirBlockId) {
        info.slope = std::max({std::abs(column(x - 1, z).height - info.height), std::abs(column(x + 1, z).height - info.height),
                               std::abs(column(x, z - 1).height - info.height), std::abs(column(x, z + 1).height - info.height)});
    }
    return m_ground.surface(info, x, z);
}
