#include "worldgen/terrain/QcTerrainGenerator.h"

#include "worldgen/terrain/QcTerrainMath.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

// The ground and the ice of a column at the edges of cold biomes (decision E-858): which biome's
// ground it takes and whether its water freezes.

namespace {

// Blocks beyond a river's half width over which still water along its line goes from the river's
// freezing threshold to its own (decision E-914).
constexpr double riverFreezeRamp = 10.0;

} // namespace

double QcTerrainGenerator::riverFreezeShare(const Column &column) const
{
    if (isRiverBiome(column.biome)) {
        return 1.0;
    }
    if (column.riverHalfWidth <= 0.0 || column.riverDistance >= QcRiverField::reach) {
        return 0.0;
    }
    // A river runs on through shallow still water (its line and bed go on there): the water along
    // it freezes like the river and gives way to the still water's threshold across the ramp, as
    // the river fades out (dry land, the end of a ring: its half width goes to 0) and towards the
    // depth where the river no longer reaches the ground. All three fall smoothly, so the threshold
    // has no step along a line.
    const double across = 1.0 - QcTerrainMath::smoothstep(column.riverHalfWidth, column.riverHalfWidth + riverFreezeRamp, column.riverDistance);
    const double presence = QcTerrainMath::smoothstep(0.0, QcRiverField::narrowestHalfWidth, column.riverHalfWidth);
    const bool exact = std::lround(column.exactHeight) == column.height;
    const double depth = seaLevel() - (exact ? column.exactHeight : static_cast<double>(column.height));
    return across * presence * (1.0 - QcTerrainMath::smoothstep(QcRiverField::deepestBed, QcRiverField::reachDown, depth));
}

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
    return m_coldEdges.freezes(column.temperature, riverFreezeShare(column), x, z) ? biome.frozenWater : m_definitions.water;
}

QcBlockId QcTerrainGenerator::surfaceBlockAt(int x, int z) const
{
    return surfaceOf(x, z, column(x, z));
}

QcBlockId QcTerrainGenerator::surfaceBlockAt(int x, int z, int height) const
{
    Column moved = column(x, z);
    moved.height = height;
    return surfaceOf(x, z, moved);
}

QcBlockId QcTerrainGenerator::surfaceOf(int x, int z, const Column &base) const
{
    Column info = withGround(x, z, base);
    if (m_definitions.biomes[info.ground()].steep != qcAirBlockId) {
        info.slope = std::max({std::abs(column(x - 1, z).height - info.height), std::abs(column(x + 1, z).height - info.height),
                               std::abs(column(x, z - 1).height - info.height), std::abs(column(x, z + 1).height - info.height)});
    }
    return m_ground.surface(info, x, z);
}
