#include "worldgen/terrain/QcTerrainSurface.h"

namespace {

// The snow line is ragged by this many blocks.
constexpr int snowLineJitter = 3;
// Share of a river's bank columns that take the river biome's beach block (sand or gravel).
constexpr double riverBeachShare = 0.7;

} // namespace

QcTerrainSurface::QcTerrainSurface(const QcTerrainDefinitions &definitions, const QcRiverField &rivers, quint64 seed)
    : m_definitions(definitions)
    , m_rivers(rivers)
    , m_seed(seed)
{
}

QcBlockId QcTerrainSurface::surface(const QcTerrainColumn &column, int x, int z) const
{
    const Biome &biome = m_definitions.biomes[column.ground()];
    const int top = column.height - 1;
    const int seaLevel = m_definitions.seaLevel;
    if (top < seaLevel - 1) {
        return patched(biome.underwater, biome.underwaterPatches, x, z);
    }
    // The land of a delta (decision E-850): sand banks flush with the water, the islands above it
    // of the surface block with its patches.
    if (biome.delta) {
        return top <= seaLevel - 1 ? biome.beach : patched(biome.surface, biome.surfacePatches, x, z);
    }
    // River banks (decision E-840): the river's beach in patches, the land's surface between them.
    if (column.riverBank && biome.riverBiome >= 0 && m_rivers.patchAt(x, z) < riverBeachShare) {
        return m_definitions.biomes[biome.riverBiome].beach;
    }
    // Beaches only where land meets the sea, not on low inland plains.
    if (top <= seaLevel + 1 && column.landFactor < 1.0) {
        return biome.beach;
    }
    if (biome.snowLine > 0
        && top >= biome.snowLine - snowLineJitter + static_cast<int>(qcPositionRandom(m_seed, x, 7, z) * (2 * snowLineJitter + 1))) {
        return biome.snow;
    }
    if (biome.steep != qcAirBlockId && column.slope >= biome.steepSlope) {
        return biome.steep;
    }
    return biome.surface;
}

QcBlockId QcTerrainSurface::patched(QcBlockId own, const QList<std::pair<QcBlockId, double>> &patches, int x, int z) const
{
    if (patches.isEmpty()) {
        return own;
    }

    // The patches take their shares of [0, 1) in turn; the rest stays the biome's own block.
    const double patch = m_rivers.patchAt(x, z);
    double below = 0.0;
    for (const auto &[block, share] : patches) {
        below += share;
        if (patch < below) {
            return block;
        }
    }
    return own;
}

QcBlockId QcTerrainSurface::subsurface(const QcTerrainColumn &column, QcBlockId surface) const
{
    const Biome &biome = m_definitions.biomes[column.ground()];
    // Under water the ground's block goes down as deep as the soil (the bed of a river in patches).
    if (column.height - 1 < m_definitions.seaLevel - 1) {
        return surface;
    }
    // A river bank's beach lies as deep as a beach.
    if (column.riverBank && biome.riverBiome >= 0 && surface == m_definitions.biomes[biome.riverBiome].beach) {
        return surface;
    }
    if (surface == biome.beach || (surface == biome.steep && biome.steep != qcAirBlockId)) {
        return surface;
    }
    return biome.subsurface;
}
