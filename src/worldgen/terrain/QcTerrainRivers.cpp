#include "worldgen/terrain/QcTerrainGenerator.h"

// The rivers and river deltas of the terrain generator (decisions E-840, E-850): the river and delta
// biomes and how they cut a terrain column.

namespace {

// The terrain's maps as the river pieces and deltas read them (decisions E-850, E-851).
class TerrainLand final : public QcRiverField::Land
{
public:
    explicit TerrainLand(const QcTerrainGenerator &terrain)
        : m_terrain(terrain)
    {
    }

    [[nodiscard]] double continentAt(int x, int z) const override { return m_terrain.continentAt(x, z); }
    [[nodiscard]] QcTerrainClimate::Point climateAt(int x, int z) const override { return m_terrain.climateAt(x, z); }
    [[nodiscard]] std::optional<QcTerrainClimate::Point> dryClimateAt(int x, int z) const override
    {
        return m_terrain.dryClimateAt(x, z);
    }

private:
    const QcTerrainGenerator &m_terrain;
};

} // namespace

QcRiverField::Sample QcTerrainGenerator::riverAt(int x, int z) const
{
    // Where column() asks no river (no rivers, ground too deep under water) there is none.
    if (!m_definitions.rivers || column(x, z).riverDistance >= QcRiverField::far) {
        return {};
    }
    return m_rivers.at(x, z, climateAt(x, z));
}

std::optional<QcTerrainClimate::Point> QcTerrainGenerator::dryClimateAt(int x, int z) const
{
    const Ground ground = groundAt(x, z);
    return ground.height >= seaLevel() ? std::optional(ground.climate) : std::nullopt;
}

bool QcTerrainGenerator::isRiverBiome(int biome) const
{
    return biome >= 0 && biome < m_definitions.biomes.size() && m_definitions.biomes[biome].river;
}

QcTerrainGenerator::RiverCut QcTerrainGenerator::riverCut(int x, int z, const QcTerrainClimate::Point &climate, double height,
                                                          int biome) const
{
    const QcRiverField::Cut cut = m_rivers.cut(x, z, climate, height, seaLevel());
    // A closed piece of a river (ending on land at both ends, decision E-851) is not built.
    if (cut.river.distance < QcRiverField::reach && cut.river.strength > 0.0 && !m_riverPieces.leadsOn(cut.river, TerrainLand(*this))) {
        return {height, biome, biome, false, {.value = cut.river.value}};
    }
    // Lands without a river biome keep their own biome along their rivers.
    const int riverBiome = m_definitions.biomes[biome].riverBiome;
    return {cut.height, cut.water && riverBiome >= 0 ? riverBiome : biome, biome, cut.bank && riverBiome >= 0, cut.river};
}


std::optional<QcTerrainGenerator::DeltaCut> QcTerrainGenerator::deltaCut(int x, int z, double continent, const RiverCut &river) const
{
    const int deltaBiome = m_definitions.biomes[river.land].deltaBiome;
    if (deltaBiome < 0) {
        return std::nullopt;
    }
    const std::optional<QcRiverDelta::Fan> fan = m_deltas.fanAt(x, z, continent, river.river.value);
    if (!fan) {
        return std::nullopt;
    }
    const QcRiverDelta::Shape shape = m_deltas.shape(*fan, TerrainLand(*this), {river.height, river.biome != river.land});
    if (shape.weight <= 0.0) {
        return std::nullopt;
    }
    // The delta's biome where the delta proper outweighs the land (not on the banks sunk further out).
    return DeltaCut{shape.height, shape.core > 0.5 ? deltaBiome : river.biome};
}

bool QcTerrainGenerator::isDeltaBiome(int biome) const
{
    return biome >= 0 && biome < m_definitions.biomes.size() && m_definitions.biomes[biome].delta;
}
