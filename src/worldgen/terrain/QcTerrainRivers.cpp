#include "worldgen/terrain/QcTerrainGenerator.h"

#include <QList>

#include <algorithm>
#include <cmath>

// The rivers and river deltas of the terrain generator (decisions E-840, E-850): the river and delta
// biomes and how they cut a terrain column.

namespace {

// touchesDelta() reads the continent in the middle of pieces of at most bandProbeStep columns
// and widens the band along the coasts by bandPad for it.
constexpr int bandProbeStep = 8;
constexpr double bandPad = 0.08;

// The probes along one axis from `from` to `to` (inclusive): the middles of pieces of at most
// bandProbeStep columns, so no column lies more than 4 columns from one.
QList<int> probeLine(int from, int to)
{
    QList<int> line;
    for (int start = from; start <= to; start += bandProbeStep) {
        line.append((start + std::min(start + bandProbeStep - 1, to)) / 2);
    }
    return line;
}

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
    // A puddle one block deep is no outlet: a river crossing it ends on land beside it (decision E-907).
    const Ground ground = groundAt(x, z);
    return ground.height >= seaLevel() - 1 ? std::optional(ground.climate) : std::nullopt;
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

bool QcTerrainGenerator::touchesDelta(QcSurfacePlanner::Rect footprint) const
{
    // As in column(): no delta without rivers and deltas. Then the two gates, and only near a delta
    // every column of the footprint.
    if (!m_definitions.rivers || !m_definitions.deltas || !nearDeltaBand(footprint) || !deltaSiteUnder(footprint)) {
        return false;
    }
    for (int z = footprint.z0; z <= footprint.z1; ++z) {
        for (int x = footprint.x0; x <= footprint.x1; ++x) {
            if (isDeltaBiome(column(x, z).biome)) {
                return true;
            }
        }
    }
    return false;
}

bool QcTerrainGenerator::nearDeltaBand(QcSurfacePlanner::Rect footprint) const
{
    // Every column lies at most 4 columns from a probe along each axis, under 6 blocks, and the
    // continent changes by at most 0.078 over 8 blocks (measured over 10^6 samples of five seeds):
    // the band widened by bandPad holds a probe near every column of the band.
    const QList<int> xs = probeLine(footprint.x0, footprint.x1);
    const QList<int> zs = probeLine(footprint.z0, footprint.z1);
    for (const int z : zs) {
        for (const int x : xs) {
            if (QcRiverDelta::mayReach(continentAt(x, z), bandPad)) {
                return true;
            }
        }
    }
    return false;
}

bool QcTerrainGenerator::deltaSiteUnder(QcSurfacePlanner::Rect footprint) const
{
    // Every column of a cell of sites reads the same site: one reading per cell (the columns read it
    // again from the thread's memory).
    const auto cellOf = [](int at) { return static_cast<int>(std::floor(at / QcDeltaSites::siteCell)); };
    const int cell = static_cast<int>(QcDeltaSites::siteCell);
    const TerrainLand land(*this);
    for (int cz = cellOf(footprint.z0); cz <= cellOf(footprint.z1); ++cz) {
        for (int cx = cellOf(footprint.x0); cx <= cellOf(footprint.x1); ++cx) {
            if (m_deltas.hasSiteAt(cx * cell + cell / 2, cz * cell + cell / 2, land)) {
                return true;
            }
        }
    }
    return false;
}
