#include "worldgen/terrain/QcTerrainGenerator.h"

#include "worldgen/dungeons/QcDungeonGenerator.h"
#include "worldgen/mineshafts/QcMineshaftGenerator.h"
#include "worldgen/surface/QcSurfaceStructures.h"
#include "worldgen/terrain/QcClimateBiomes.h"
#include "worldgen/terrain/QcTerrainColumnGrid.h"
#include "worldgen/terrain/QcTerrainMath.h"
#include "worldgen/terrain/QcTreeShape.h"
#include "worldgen/towers/QcCoastalTowerGenerator.h"
#include "worldgen/villages/QcVillageGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>

using QcTerrainMath::smoothstep;

namespace {

// Horizontal scales of the noise fields in blocks.
constexpr double continentScale = 900.0;
constexpr double detailScale = 160.0;
constexpr double hillScale = 48.0;
constexpr double ridgeScale = 240.0;
// Biome borders and coasts are bent by up to this many blocks.
constexpr double warpScale = 420.0;
constexpr double warpAmplitude = 140.0;
// Terraces form only in patches of about this size.
constexpr double terraceScale = 380.0;
// Ocean floor lies this far below sea level (before detail noise).
constexpr double oceanDepth = 12.0;
constexpr int featureMargin = QcTerrainColumnGrid::featureMargin;
// The runner-up biome's share of the heaviest's weight from which a column may lie close enough to
// a border for its ground to come from the other biome (decision E-858).
constexpr double nearBorderWeight = 0.05;

// Terraces: flat steps of `step` blocks with a cliff at the end of each.
double terraced(double height, int step)
{
    const double level = height / step;
    const double floor = std::floor(level);
    return (floor + smoothstep(0.97, 1.0, level - floor)) * step;
}

// The value that chooses the kind of a scattered vegetation plant at a column (decision E-814): the
// same over areas of about patchSize / 2 blocks with frayed borders, per column without a patch size.
double kindRandom(const QcTerrainDefinitions::Feature &feature, int x, int z)
{
    if (feature.patchSize < 4) {
        return qcPositionRandom(feature.seed, x, 2, z);
    }

    // Areas of one kind: cells of half the patch size, their borders bent by the patch noise.
    const int size = feature.patchSize / 2;
    const double warpX = feature.patches.noise2(x / 9.0 + 31.7, z / 9.0) * size * 0.5;
    const double warpZ = feature.patches.noise2(x / 9.0, z / 9.0 + 57.3) * size * 0.5;
    const int areaX = int(std::floor((x + warpX) / size));
    const int areaZ = int(std::floor((z + warpZ) / size));
    return qcPositionRandom(feature.seed, areaX, 2, areaZ);
}

} // namespace

QcTerrainGenerator::QcTerrainGenerator(std::shared_ptr<const QcContentRegistry> content, qint64 seed)
    : m_content(std::move(content))
    , m_seed(quint64(seed))
    , m_definitions(*m_content, quint64(seed))
    , m_caves(quint64(seed), m_content->worldgen().terrainSettings(), m_definitions)
    , m_pools(m_content, m_definitions)
    , m_climate(quint64(seed), m_definitions.anyRare)
    , m_rivers(quint64(seed))
    , m_coldEdges(m_definitions.coldEdges, quint64(seed))
    , m_ground(m_definitions, m_rivers, quint64(seed))
    , m_continentNoise(qcMixSeed(quint64(seed), 1))
    , m_detailNoise(qcMixSeed(quint64(seed), 2))
    , m_warpNoiseX(qcMixSeed(quint64(seed), 9))
    , m_warpNoiseZ(qcMixSeed(quint64(seed), 10))
    , m_hillNoise(qcMixSeed(quint64(seed), 11))
    , m_ridgeNoise(qcMixSeed(quint64(seed), 12))
    , m_terraceNoise(qcMixSeed(quint64(seed), 13))
    , m_overhangNoise(qcMixSeed(quint64(seed), 16))
{
    m_villages = std::make_unique<QcVillageGenerator>(*this, m_content, m_seed);
    m_dungeons = std::make_unique<QcDungeonGenerator>(*this, *m_content, m_seed);
    m_mineshafts = std::make_unique<QcMineshaftGenerator>(*this, *m_content, m_seed);
    m_surface = std::make_unique<QcSurfaceStructures>(*this, *m_villages, *m_content, m_seed);
    m_coastalTowers = std::make_unique<QcCoastalTowerGenerator>(*this, *m_content, m_seed);
    m_vegetationPatches = std::make_unique<QcVegetationPatchPlacer>(*this, m_definitions, m_seed);
    m_giantTrees = std::make_unique<QcGiantTreePlacer>(*this, m_definitions, m_seed);
    m_lakes = std::make_unique<QcSurfaceLakes>(*this, m_definitions, m_content);
    m_warnings = m_definitions.warnings + m_villages->styles().warnings() + m_dungeons->planner().warnings()
                 + m_mineshafts->planner().warnings() + m_surface->planner().warnings() + m_coastalTowers->planner().warnings();
}

QcChunkFurnishing QcTerrainGenerator::furnishingIn(QcChunkPos pos) const
{
    QcChunkFurnishing furnishing = m_dungeons->furnishingIn(pos);
    furnishing.add(m_mineshafts->furnishingIn(pos));
    furnishing.add(m_coastalTowers->furnishingIn(pos));
    furnishing.add(m_surface->furnishingIn(pos));
    if (const std::shared_ptr<const QcVillagePlan> village = m_villages->planFor(pos)) {
        furnishing.add(QcVillageGenerator::furnishingIn(pos, *village));
    }
    return furnishing;
}

QList<QcGuardSite> QcTerrainGenerator::guardSitesNear(QcBlockPos from, int reach) const
{
    return m_surface->sitesNear(from, reach) + m_villages->sitesNear(from, reach);
}

std::optional<QList<QcGuardSite>> QcTerrainGenerator::plannedGuardSitesNear(QcBlockPos from, int reach) const
{
    const std::optional<QList<QcGuardSite>> surface = m_surface->plannedSitesNear(from, reach);
    const std::optional<QList<QcGuardSite>> villages = m_villages->plannedSitesNear(from, reach);
    if (!surface || !villages) {
        return std::nullopt;
    }
    return *surface + *villages;
}

std::optional<QcStructurePlacement> QcTerrainGenerator::nearestStructure(const QString &name, QcBlockPos from, int reach) const
{
    // Alone or at the centre of a village (decision E-592).
    std::optional<QcStructurePlacement> alone = m_surface->nearest(name, from, reach);
    std::optional<QcStructurePlacement> inVillage = m_villages->nearestPiece(name, from, reach);
    const auto distance = [from](const QcStructurePlacement &found) {
        return std::hypot(found.origin().x + found.sizeX() / 2.0 - from.x, found.origin().z + found.sizeZ() / 2.0 - from.z);
    };
    if (!alone || (inVillage && distance(*inVillage) < distance(*alone))) {
        return inVillage;
    }
    return alone;
}

QcTerrainGenerator::~QcTerrainGenerator() = default;

std::shared_ptr<const QcVillagePlan> QcTerrainGenerator::villageFor(QcChunkPos pos) const
{
    return m_villages->planFor(pos);
}

std::shared_ptr<const QcVillagePlan> QcTerrainGenerator::nearestVillage(QcBlockPos from, int reach) const
{
    return m_villages->nearest(from, reach);
}

QString QcTerrainGenerator::biomeName(int index) const
{
    return index >= 0 && index < m_definitions.biomes.size() ? m_definitions.biomes[index].name : QString();
}

double QcTerrainGenerator::biomeHeight(const Biome &biome, double rolling, double ridge, double terraces) const
{
    double height = biome.baseHeight + biome.heightVariation * ((1.0 - biome.ridges) * rolling + biome.ridges * ridge);
    if (biome.terraceHeight > 0 && terraces > 0.0) {
        height += (terraced(height, biome.terraceHeight) - height) * terraces;
    }
    return height;
}

QcTerrainClimate::Point QcTerrainGenerator::climateAt(int x, int z) const
{
    const double warpedX = x + warpAmplitude * m_warpNoiseX.fractal2(x / warpScale, z / warpScale, 3);
    const double warpedZ = z + warpAmplitude * m_warpNoiseZ.fractal2(x / warpScale, z / warpScale, 3);
    return m_climate.at(x, z, warpedX, warpedZ);
}

int QcTerrainGenerator::biomeForClimate(const QcTerrainClimate::Point &climate) const
{
    return QcClimateBiomes::choose(m_definitions.biomes, climate);
}

QcRiverField::Sample QcTerrainGenerator::riverAt(int x, int z) const
{
    // Where column() asks no river (no rivers, ground too deep under water) there is none.
    if (!m_definitions.rivers || column(x, z).riverDistance >= QcRiverField::far) {
        return {};
    }
    return m_rivers.at(x, z, climateAt(x, z));
}

bool QcTerrainGenerator::isRiverBiome(int biome) const
{
    return biome >= 0 && biome < m_definitions.biomes.size() && m_definitions.biomes[biome].river;
}

QcTerrainGenerator::RiverCut QcTerrainGenerator::riverCut(int x, int z, const QcTerrainClimate::Point &climate, double height,
                                                          int biome) const
{
    const QcRiverField::Cut cut = m_rivers.cut(x, z, climate, height, seaLevel());
    // Lands without a river biome keep their own biome along their rivers.
    const int riverBiome = m_definitions.biomes[biome].riverBiome;
    return {cut.height, cut.water && riverBiome >= 0 ? riverBiome : biome, cut.bank && riverBiome >= 0, cut.river};
}

QcTerrainGenerator::Blend QcTerrainGenerator::blend(const QcTerrainClimate::Point &climate, double rolling, double ridge, double terraces) const
{
    // Inverse-distance weighting in climate space blends heights smoothly across biome borders. All
    // biomes take part: the mountains' height reaches far into the lowland through their weight, and
    // a cut-off made the slopes there steep (decision E-824).
    double weightSum = 0.0;
    double landHeight = 0.0;
    double overhang = 0.0;
    double bestWeight = -1.0;
    // The nearest biome in climate space with the rarity left out.
    double nearestWeight = -1.0;
    int nearest = 0;
    Blend result;
    for (qsizetype i = 0; i < m_definitions.biomes.size(); ++i) {
        const Biome &biome = m_definitions.biomes[i];
        // River biomes take the channels of rivers, never a climate (decision E-840).
        if (biome.river) {
            continue;
        }
        double weight = QcClimateBiomes::weight(biome, climate);
        if (weight > nearestWeight) {
            nearestWeight = weight;
            nearest = int(i);
        }
        weight *= QcClimateBiomes::rarityGate(biome, climate.rarity);
        weightSum += weight;
        landHeight += weight * biomeHeight(biome, rolling, ridge, terraces);
        overhang += weight * biome.overhang;
        if (weight > bestWeight) {
            result.runnerUpWeight = bestWeight;
            bestWeight = weight;
            result.biome = int(i);
        } else {
            result.runnerUpWeight = std::max(result.runnerUpWeight, weight);
        }
    }
    result.bestWeight = bestWeight;

    if (weightSum <= 0.0) {
        // Every biome is rare and the column lies outside the spots (only with mods, warned at load,
        // decision E-815): the nearest biome alone.
        const Biome &biome = m_definitions.biomes[nearest];
        result.biome = nearest;
        result.landHeight = biomeHeight(biome, rolling, ridge, terraces);
        result.overhang = biome.overhang;
        return result;
    }
    result.landHeight = landHeight / weightSum;
    result.overhang = overhang / weightSum;
    return result;
}

QcTerrainGenerator::Column QcTerrainGenerator::column(int x, int z) const
{
    // Bent coordinates for the large-scale maps, so biome borders and coasts meander (climateAt bends
    // the same way).
    const double warpedX = x + warpAmplitude * m_warpNoiseX.fractal2(x / warpScale, z / warpScale, 3);
    const double warpedZ = z + warpAmplitude * m_warpNoiseZ.fractal2(x / warpScale, z / warpScale, 3);
    const QcTerrainClimate::Point climate = m_climate.at(x, z, warpedX, warpedZ);

    // Rolling hills: broad swells with smaller hills on top, in [-1, 1].
    const double detail = std::clamp(1.6 * m_detailNoise.fractal2(x / detailScale, z / detailScale, 5), -1.0, 1.0);
    const double rolling = std::clamp(0.75 * detail + 0.6 * m_hillNoise.fractal2(x / hillScale, z / hillScale, 3), -1.0, 1.0);
    // Ridges: sharp crests where the noise crosses zero, in about [-0.6, 1]; the valleys between
    // them stay above the sea.
    double ridge = 0.0;
    if (m_definitions.anyRidges) {
        const double crest = 1.0 - std::min(1.0, 2.2 * std::abs(m_ridgeNoise.fractal2(x / ridgeScale, z / ridgeScale, 4)));
        ridge = 1.6 * crest * crest - 0.6 + 0.2 * detail;
    }
    const double terraces =
        m_definitions.anyTerraces ? smoothstep(-0.1, 0.15, m_terraceNoise.fractal2(x / terraceScale, z / terraceScale, 2)) : 0.0;

    const Blend blended = blend(climate, rolling, ridge, terraces);
    Column result;
    result.biome = blended.biome;

    const double continent = 1.4 * m_continentNoise.fractal2(warpedX / continentScale, warpedZ / continentScale, 4);
    result.landFactor = smoothstep(-0.30, -0.05, continent);
    const double oceanFloor = seaLevel() - oceanDepth + 5.0 * detail;
    double height = oceanFloor + (blended.landHeight - oceanFloor) * result.landFactor;
    if (m_definitions.rivers) {
        const RiverCut cut = riverCut(x, z, climate, height, blended.biome);
        height = cut.height;
        result.biome = cut.biome;
        result.riverBank = cut.bank;
        result.riverDistance = cut.river.distance;
        result.riverHalfWidth = cut.river.halfWidth;
    }
    result.temperature = climate.temperature;
    result.landBiome = blended.biome;
    // Only near a border can the jittered column lie in another biome: there the next biome's
    // weight comes close to the heaviest's (weights fall with the fourth power of the distance).
    const bool nearBorder = m_coldEdges.groundBlend() > 0 && blended.runnerUpWeight >= nearBorderWeight * blended.bestWeight;
    result.groundBiome = nearBorder ? Column::unresolvedGround : -1;
    result.height = std::clamp(int(std::lround(height)), 4, QcWorld::worldHeight - 8);
    // Overhangs fade out towards the coast and never reach down to the sea level, so no hollow
    // lies next to or under water.
    const double overhangAmplitude = std::min(blended.overhang * result.landFactor, double(result.height - seaLevel() - 3));
    result.overhang = overhangAmplitude < 1.0 ? 0.0 : overhangAmplitude;
    return result;
}

bool QcTerrainGenerator::isCave(int x, int y, int z) const
{
    if (!m_caves.enabled() || y <= 0) {
        return false;
    }
    return m_caves.isCave(x, y, z, column(x, z));
}

bool QcTerrainGenerator::isCave(int x, int y, int z, const Column &column) const
{
    return m_caves.enabled() && y > 0 && m_caves.isCave(x, y, z, column);
}

QcTerrainGenerator::OverhangBand QcTerrainGenerator::overhangBand(int x, int z, const Column &column) const
{
    const double amplitude = column.overhang;
    OverhangBand band;
    band.bottom = std::max(1, int(std::floor(column.height - amplitude)));
    const int end = std::min(QcWorld::worldHeight, int(std::ceil(column.height + amplitude)) + 1);
    band.size = std::min(end - band.bottom, maxOverhangBand);

    // A 3D noise reshapes the ground around the surface. Below the plain height it cuts hollows
    // under solid ground (overhanging cliffs, ledges, arches); above it, it only piles up blocks
    // without gaps, so nothing floats.
    bool stacked = true;
    std::array<bool, maxOverhangBand> &solid = band.solid;
    for (int i = 0; i < band.size; ++i) {
        const int y = band.bottom + i;
        const double density = (column.height - 0.5 - y) / amplitude + 3.0 * m_overhangNoise.fractal3(x / 16.0, y / 7.0, z / 16.0, 2);
        if (y < column.height - amplitude) {
            solid[std::size_t(i)] = true;
        } else if (y >= column.height + amplitude) {
            solid[std::size_t(i)] = false;
        } else if (y < column.height) {
            solid[std::size_t(i)] = density > 0.0;
        } else {
            stacked = stacked && density > 0.0;
            solid[std::size_t(i)] = stacked;
        }
    }
    return band;
}

int QcTerrainGenerator::groundTop(int x, int z, const Column &column) const
{
    if (column.overhang < 1.0) {
        return column.height - 1;
    }

    const OverhangBand band = overhangBand(x, z, column);
    for (int i = band.size - 1; i >= 0; --i) {
        if (band.solid[std::size_t(i)]) {
            return band.bottom + i;
        }
    }
    return band.bottom - 1;
}

void QcTerrainGenerator::fillColumn(QcChunk &chunk, int localX, int localZ, const Column &column) const
{
    const Biome &biome = m_definitions.biomes[column.ground()];
    const QcBlockId stone = m_definitions.stone;
    const QcBlockId bedrock = m_definitions.bedrock;
    const int bedrockLayers = m_definitions.bedrockLayers;
    const int worldX = chunk.pos().x * QcWorld::chunkSize + localX;
    const int worldZ = chunk.pos().z * QcWorld::chunkSize + localZ;
    const QcBlockId surface = m_ground.surface(column, worldX, worldZ);
    const QcBlockId subsurface = m_ground.subsurface(column, surface);
    const bool overhangs = column.overhang >= 1.0;
    const OverhangBand band = overhangs ? overhangBand(worldX, worldZ, column) : OverhangBand{.bottom = column.height};
    const int highest = overhangs ? band.bottom + band.size - 1 : column.height - 1;
    const auto solidAt = [&](int y) { return y < band.bottom || (y - band.bottom < band.size && band.solid[std::size_t(y - band.bottom)]); };

    // From the top down: the first solid block under air is the surface, the next ones the soil.
    int depth = -1;
    for (int y = highest; y >= 0; --y) {
        if (!solidAt(y)) {
            depth = -1;
            continue;
        }

        depth += 1;
        QcBlockId block = stone;
        if (depth == 0) {
            block = surface;
        } else if (depth <= biome.subsurfaceDepth) {
            block = subsurface;
        }

        // Sand and gravel would fall into the hollows of an overhang band: rock instead.
        if (overhangs && block != stone && m_content->isValidBlock(block) && m_content->block(block).falls) {
            block = stone;
        }

        // Solid at the bottom, thinning out over the layers above.
        if (y == 0 || (y < bedrockLayers && qcPositionRandom(m_seed, worldX, y, worldZ) < double(bedrockLayers - y) / bedrockLayers)) {
            block = bedrock;
        }

        chunk.setBlock(localX, y, localZ, block);
    }

    const QcBlockId top = frozenWaterAt(worldX, worldZ, column);
    for (int y = column.height; y < seaLevel(); ++y) {
        chunk.setBlock(localX, y, localZ, y == seaLevel() - 1 ? top : m_definitions.water);
    }
}

QList<bool> QcTerrainGenerator::carvedColumns(const QcChunk &chunk, const QcTerrainColumnGrid &columns) const
{
    const QcChunkPos pos = chunk.pos();
    QList<bool> carved(qsizetype(QcWorld::chunkSize) * QcWorld::chunkSize);
    for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
        for (int lx = 0; lx < QcWorld::chunkSize; ++lx) {
            const int worldX = pos.x * QcWorld::chunkSize + lx;
            const int worldZ = pos.z * QcWorld::chunkSize + lz;
            const int top = groundTop(worldX, worldZ, columns.at(worldX, worldZ));
            carved[lz * QcWorld::chunkSize + lx] = top >= 0 && chunk.block(lx, top, lz) == qcAirBlockId;
        }
    }
    return carved;
}

QcTerrainGenerator::Vein QcTerrainGenerator::grownVein(const Ore &ore, int vein, QcChunkPos pos) const
{
    constexpr std::array<std::array<int, 3>, 6> directions{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    const auto random = [&](int salt) { return qcPositionRandom(ore.seed, pos.x, vein * 1024 + salt, pos.z); };

    // A compact clump: each block grows from a random earlier one onto a free face, kept inside
    // this chunk so veins never depend on neighbours.
    Vein cells;
    cells.append({int(random(0) * QcWorld::chunkSize), ore.minY + int(random(1) * (ore.maxY - ore.minY + 1)),
                  int(random(2) * QcWorld::chunkSize)});
    for (int step = 1, tries = 0; step < ore.veinSize && tries < 4 * ore.veinSize; ++tries) {
        const QcBlockPos from = cells[qsizetype(random(3 + tries * 2) * double(cells.size()))];
        const auto &direction = directions[std::size_t(random(4 + tries * 2) * 6.0)];
        const QcBlockPos next{from.x + direction[0], from.y + direction[1], from.z + direction[2]};
        if (next.x < 0 || next.x >= QcWorld::chunkSize || next.z < 0 || next.z >= QcWorld::chunkSize || next.y < ore.minY
            || next.y > ore.maxY || cells.contains(next)) {
            continue;
        }
        cells.append(next);
        ++step;
    }
    return cells;
}

void QcTerrainGenerator::placeVein(QcChunk &chunk, const Ore &ore, const Vein &cells) const
{
    for (const QcBlockPos &cell : cells) {
        if (cell.y >= 0 && cell.y < QcWorld::worldHeight && chunk.block(cell.x, cell.y, cell.z) == ore.replaces) {
            chunk.setBlock(cell.x, cell.y, cell.z, ore.block);
        }
    }
}

void QcTerrainGenerator::placeOres(QcChunk &chunk, const QcTerrainColumnGrid &columns) const
{
    const QcChunkPos pos = chunk.pos();
    for (const Ore &ore : m_definitions.ores) {
        for (int vein = 0; vein < ore.veins; ++vein) {
            const Vein cells = grownVein(ore, vein, pos);
            // A vein bound to biomes lies where the column of its first block has one of them.
            const int startBiome = columns.at(pos.x * QcWorld::chunkSize + cells.first().x, pos.z * QcWorld::chunkSize + cells.first().z).biome;
            if (ore.biomes.isEmpty() || ore.biomes.contains(startBiome)) {
                placeVein(chunk, ore, cells);
            }
        }
    }
}

void QcTerrainGenerator::placeFeature(QcChunk &chunk, const Feature &feature, QcBlockPos base, double random) const
{
    QcTreeShape::buildInChunk(chunk, feature.shape, base, random, m_seed);
}

std::optional<int> QcTerrainGenerator::chosenFeature(const Column &column, int x, int z, Spot spot) const
{
    for (const auto &[featureIndex, density] : m_definitions.biomes[column.biome].features) {
        const Feature &feature = m_definitions.features[featureIndex];
        const Spot wanted = feature.onWater ? Spot::Water : (feature.nearWater ? Spot::Shore : Spot::Land);
        if (wanted != spot) {
            continue;
        }

        double localDensity = density;
        if (feature.patchSize > 0) {
            // Patches: dense where the noise is high, none where it is low; symmetric around 1, so
            // the average density stays about the same.
            const double patch = feature.patches.fractal2(x / double(feature.patchSize), z / double(feature.patchSize), 2);
            localDensity *= std::clamp(1.0 + 3.0 * patch, 0.0, 2.0);
        }
        if (qcPositionRandom(feature.seed, x, 0, z) < localDensity) {
            return featureIndex;
        }
    }
    return std::nullopt;
}

void QcTerrainGenerator::placeFeatureInColumn(QcChunk &chunk, const QcTerrainColumnGrid &columns, const QList<bool> &carved, int x,
                                              int z) const
{
    const Column &info = columns.at(x, z);
    if (m_definitions.biomes[info.biome].features.isEmpty()) {
        return;
    }
    if (info.height < seaLevel()) {
        placeFeatureOnWater(chunk, info, carved, x, z);
        return;
    }
    if (info.height - 1 < seaLevel()) {
        return;
    }

    // Ground at sea level only takes features that want water next to them, and only where a
    // neighbouring column is under water.
    const bool shore = info.height - 1 == seaLevel();
    const std::optional<int> featureIndex = chosenFeature(info, x, z, shore ? Spot::Shore : Spot::Land);
    if (!featureIndex) {
        return;
    }

    const Feature &feature = m_definitions.features[*featureIndex];
    const int baseX = chunk.pos().x * QcWorld::chunkSize;
    const int baseZ = chunk.pos().z * QcWorld::chunkSize;
    const auto waterBeside = [&] {
        return columns.at(x + 1, z).height - 1 < seaLevel() || columns.at(x - 1, z).height - 1 < seaLevel()
               || columns.at(x, z + 1).height - 1 < seaLevel() || columns.at(x, z - 1).height - 1 < seaLevel();
    };
    const bool inside = x >= baseX && x < baseX + QcWorld::chunkSize && z >= baseZ && z < baseZ + QcWorld::chunkSize;
    // Only trees reach into this chunk from a column outside it.
    if ((shore && !waterBeside()) || (!inside && feature.shape.type != QcFeatureDefinition::Type::Tree)) {
        return;
    }
    // A vegetation layer (decision E-814) chooses its plant here, and the plant's own ground counts.
    const QcVegetationTable::Plant *plant =
        feature.plants.isEmpty() ? nullptr : &feature.plants.pick(kindRandom(feature, x, z));
    const QSet<QcBlockId> &allowedGround = plant ? plant->ground : feature.ground;
    if (!allowedGround.isEmpty() && !allowedGround.contains(m_ground.surface(withGround(x, z, info), x, z))) {
        return;
    }

    // Skip columns whose ground was carved away by a cave opening. Inside the chunk the carving
    // itself tells (cheaper than the noise, recorded before pools and features changed blocks);
    // shores keep a lid, so ask the noise like the neighbours do.
    const int top = groundTop(x, z, info);
    const bool open = inside && !shore ? carved[(z - baseZ) * QcWorld::chunkSize + (x - baseX)] : isCave(x, top, z);
    if (open) {
        return;
    }

    if (plant) {
        if (top + 1 < QcWorld::worldHeight && chunk.block(x - baseX, top + 1, z - baseZ) == qcAirBlockId) {
            chunk.setBlock(x - baseX, top + 1, z - baseZ, plant->block);
        }
        return;
    }
    placeFeature(chunk, feature, {x, top + 1, z}, qcPositionRandom(feature.seed, x, 1, z));
}

void QcTerrainGenerator::placeFeatureOnWater(QcChunk &chunk, const Column &column, const QList<bool> &carved, int x, int z) const
{
    const int localX = x - chunk.pos().x * QcWorld::chunkSize;
    const int localZ = z - chunk.pos().z * QcWorld::chunkSize;
    if (localX < 0 || localX >= QcWorld::chunkSize || localZ < 0 || localZ >= QcWorld::chunkSize) {
        return;
    }
    // Like on land: nothing over ground a cave opening carved away.
    if (carved[localZ * QcWorld::chunkSize + localX]) {
        return;
    }

    const std::optional<int> featureIndex = chosenFeature(column, x, z, Spot::Water);
    if (!featureIndex) {
        return;
    }

    // The plant stands on the top water block (ice in a frozen biome is no water) in the air above it.
    const Feature &feature = m_definitions.features[*featureIndex];
    const QcBlockId surface = chunk.block(localX, seaLevel() - 1, localZ);
    if ((!feature.ground.isEmpty() && !feature.ground.contains(surface)) || chunk.block(localX, seaLevel(), localZ) != qcAirBlockId) {
        return;
    }
    placeFeature(chunk, feature, {x, seaLevel(), z}, qcPositionRandom(feature.seed, x, 1, z));
}

void QcTerrainGenerator::placeFeatures(QcChunk &chunk, const QcTerrainColumnGrid &columns, const QList<bool> &carved,
                                       const QcVillagePlan *village, const QList<QcSurfacePlanner::Rect> &structures) const
{
    const int baseX = chunk.pos().x * QcWorld::chunkSize;
    const int baseZ = chunk.pos().z * QcWorld::chunkSize;
    // Trees reach featureMargin blocks from their trunk: none grows into a village, nor into a
    // surface structure, where it would be cut or left floating.
    const auto allowed = [&](int x, int z) {
        const QcSurfacePlanner::Rect trunk{x, z, x, z};
        return !(village && village->covers(x, z, featureMargin))
               && std::ranges::none_of(structures, [&](QcSurfacePlanner::Rect footprint) { return footprint.intersects(trunk, featureMargin); });
    };
    for (int z = baseZ - featureMargin; z < baseZ + QcWorld::chunkSize + featureMargin; ++z) {
        for (int x = baseX - featureMargin; x < baseX + QcWorld::chunkSize + featureMargin; ++x) {
            if (allowed(x, z)) {
                placeFeatureInColumn(chunk, columns, carved, x, z);
            }
        }
    }
    // Patches of one plant kind after the scattered plants (decision E-814).
    m_vegetationPatches->place(chunk, columns, carved, allowed);
}

QList<QcSurfacePlanner::Rect> QcTerrainGenerator::structureFootprintsNear(QcChunkPos pos, int margin) const
{
    QList<QcSurfacePlanner::Rect> structures = m_surface->footprintsNear(pos, margin);
    const QList<QcCoastalTowerPlan::Footprint> towers = m_coastalTowers->footprintsNear(pos, margin);
    for (const QcCoastalTowerPlan::Footprint &tower : towers) {
        structures.append({tower.minX, tower.minZ, tower.maxX, tower.maxZ});
    }
    // A lake lies inside its chunk: the chunks within the margin.
    const int reach = (margin + QcWorld::chunkSize - 1) / QcWorld::chunkSize;
    const QcSurfacePlanner::Rect area{pos.x * QcWorld::chunkSize, pos.z * QcWorld::chunkSize, pos.x * QcWorld::chunkSize + QcWorld::chunkSize - 1,
                                      pos.z * QcWorld::chunkSize + QcWorld::chunkSize - 1};
    for (int dz = -reach; dz <= reach; ++dz) {
        for (int dx = -reach; dx <= reach; ++dx) {
            if (const std::optional<QcSurfaceLakes::Site> lake = lakeIn({pos.x + dx, pos.z + dz}); lake && lake->footprint.intersects(area, margin)) {
                structures.append(lake->footprint);
            }
        }
    }
    return structures;
}

std::optional<QcSurfaceLakes::Site> QcTerrainGenerator::lakeIn(QcChunkPos pos) const
{
    std::optional<QcSurfaceLakes::Site> site = m_lakes->siteIn(pos);
    if (!site) {
        return site;
    }
    // Like the trees: none in or near a village, a surface structure or a coastal tower.
    const std::shared_ptr<const QcVillagePlan> village = m_villages->planFor(pos);
    const QList<QcSurfacePlanner::Rect> structures = m_surface->footprintsNear(pos, featureMargin);
    const QList<QcCoastalTowerPlan::Footprint> towers = m_coastalTowers->footprintsNear(pos, featureMargin);
    if ((village && village->overlaps(pos, featureMargin)) || !structures.isEmpty() || !towers.isEmpty()
        || mineshaftEntranceNear(pos, site->footprint)) {
        return std::nullopt;
    }
    return site;
}

bool QcTerrainGenerator::mineshaftEntranceNear(QcChunkPos pos, QcSurfacePlanner::Rect footprint) const
{
    // A shaft's ladder runs from the ground's top down: through a lake it would let the liquid out.
    const QList<std::shared_ptr<const QcMineshaftPlan>> plans = m_mineshafts->plansTouching(pos);
    return std::ranges::any_of(plans, [&](const std::shared_ptr<const QcMineshaftPlan> &plan) {
        return plan->entrance && footprint.intersects({plan->entrance->top.x, plan->entrance->top.z, plan->entrance->top.x, plan->entrance->top.z}, featureMargin);
    });
}

std::shared_ptr<QcChunk> QcTerrainGenerator::generate(QcChunkPos pos) const
{
    auto chunk = std::make_shared<QcChunk>(pos);
    const QcTerrainColumnGrid columns(*this, pos);
    for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
        for (int lx = 0; lx < QcWorld::chunkSize; ++lx) {
            fillColumn(*chunk, lx, lz, columns.at(pos.x * QcWorld::chunkSize + lx, pos.z * QcWorld::chunkSize + lz));
        }
    }

    m_caves.carve(*chunk, columns);
    const QList<bool> carved = carvedColumns(*chunk, columns);
    placeOres(*chunk, columns);
    m_pools.placeInCaves(*chunk);
    // Dungeons and mine shafts after the cave pools, so their corridors open into finished caves.
    m_dungeons->build(*chunk);
    m_mineshafts->build(*chunk);
    // The village of the chunk's region: no features or surface pools there, its pieces last. None
    // near a surface structure or a coastal tower either (decisions E-630, E-605).
    const std::shared_ptr<const QcVillagePlan> village = m_villages->planFor(pos);
    // Trees of the margin columns stand up to featureMargin beyond the chunk and keep featureMargin
    // from a footprint: twice the margin, so each chunk a tree reaches decides it alike.
    const QList<QcSurfacePlanner::Rect> structures = structureFootprintsNear(pos, 2 * featureMargin);
    placeFeatures(*chunk, columns, carved, village.get(), structures);
    // Giant trees after the small ones (decision E-822).
    m_giantTrees->build(*chunk, village.get());
    if (const std::optional<QcSurfaceLakes::Site> lake = lakeIn(pos)) {
        m_lakes->carve(*chunk, *lake, columns);
    }
    // Mine shaft entrances over the trees, cacti and pools (none lies near a village).
    m_mineshafts->buildEntrances(*chunk);
    // Coastal towers over the trees and pools too (none lies near a village, an entrance or a
    // surface structure).
    m_coastalTowers->build(*chunk);
    // Surface structures over the terrain (no features or pools near them), never near a village.
    m_surface->build(*chunk);
    if (village) {
        m_villages->build(*chunk, *village);
    }

    for (int i = 0; i < QcWorld::sectionCount; ++i) {
        chunk->section(i).compact();
    }
    return chunk;
}
