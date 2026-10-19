#include "worldgen/terrain/QcTerrainGenerator.h"

#include "worldgen/dungeons/QcDungeonGenerator.h"
#include "worldgen/mineshafts/QcMineshaftGenerator.h"
#include "worldgen/surface/QcSurfaceStructures.h"
#include "worldgen/terrain/QcTerrainColumnGrid.h"
#include "worldgen/terrain/QcTerrainMath.h"
#include "worldgen/terrain/QcTreeShape.h"
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
constexpr double temperatureScale = 1900.0;
constexpr double humidityScale = 1600.0;
constexpr double ruggednessScale = 1500.0;
// Biome borders and coasts are bent by up to this many blocks.
constexpr double warpScale = 420.0;
constexpr double warpAmplitude = 140.0;
// The climate maps are bent a second time on a smaller scale, which frays biome borders into lobes
// and tongues instead of round patches (decision E-101).
constexpr double climateWarpScale = 160.0;
constexpr double climateWarpAmplitude = 200.0;
// Terraces form only in patches of about this size.
constexpr double terraceScale = 380.0;
// Spreads the climate noise (about +-0.3 around 0) over most of [0, 1].
constexpr double climateContrast = 1.5;
// Ruggedness only rises where the ruggedness noise is near zero: long winding chains.
constexpr double ruggednessSharpness = 6.0;
// The ruggedness axis counts twice as much as temperature and humidity.
constexpr double ruggednessWeight = 2.0;
// Ocean floor lies this far below sea level (before detail noise).
constexpr double oceanDepth = 12.0;
constexpr int featureMargin = QcTerrainColumnGrid::featureMargin;
// The snow line is ragged by this many blocks.
constexpr int snowLineJitter = 3;
constexpr int spawnSearchRadius = 512;

// Terraces: flat steps of `step` blocks with a cliff at the end of each.
double terraced(double height, int step)
{
    const double level = height / step;
    const double floor = std::floor(level);
    return (floor + smoothstep(0.97, 1.0, level - floor)) * step;
}

} // namespace

QcTerrainGenerator::QcTerrainGenerator(std::shared_ptr<const QcContentRegistry> content, qint64 seed, QStringList *warnings)
    : m_content(std::move(content))
    , m_seed(quint64(seed))
    , m_definitions(*m_content, quint64(seed), warnings)
    , m_caves(quint64(seed), m_content->terrainSettings(), m_definitions)
    , m_pools(m_content, m_definitions)
    , m_continentNoise(qcMixSeed(quint64(seed), 1))
    , m_detailNoise(qcMixSeed(quint64(seed), 2))
    , m_temperatureNoise(qcMixSeed(quint64(seed), 3))
    , m_humidityNoise(qcMixSeed(quint64(seed), 4))
    , m_ruggednessNoise(qcMixSeed(quint64(seed), 8))
    , m_warpNoiseX(qcMixSeed(quint64(seed), 9))
    , m_warpNoiseZ(qcMixSeed(quint64(seed), 10))
    , m_climateWarpNoiseX(qcMixSeed(quint64(seed), 18))
    , m_climateWarpNoiseZ(qcMixSeed(quint64(seed), 19))
    , m_hillNoise(qcMixSeed(quint64(seed), 11))
    , m_ridgeNoise(qcMixSeed(quint64(seed), 12))
    , m_terraceNoise(qcMixSeed(quint64(seed), 13))
    , m_overhangNoise(qcMixSeed(quint64(seed), 16))
{
    m_villages = std::make_unique<QcVillageGenerator>(*this, m_content, m_seed, warnings);
    m_dungeons = std::make_unique<QcDungeonGenerator>(*this, *m_content, m_seed, warnings);
    m_mineshafts = std::make_unique<QcMineshaftGenerator>(*this, *m_content, m_seed, warnings);
    m_surface = std::make_unique<QcSurfaceStructures>(*this, *m_villages, *m_content, m_seed, warnings);
}

QcChunkFurnishing QcTerrainGenerator::furnishingIn(QcChunkPos pos) const
{
    QcChunkFurnishing furnishing = m_dungeons->furnishingIn(pos);
    m_mineshafts->addFurnishing(pos, furnishing);
    m_surface->addFurnishing(pos, furnishing);
    if (const std::shared_ptr<const QcVillagePlan> village = m_villages->planFor(pos)) {
        m_villages->addFurnishing(pos, *village, furnishing);
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

QcTerrainGenerator::Column QcTerrainGenerator::column(int x, int z) const
{
    // Bent coordinates for the large-scale maps, so biome borders and coasts meander.
    const double warpedX = x + warpAmplitude * m_warpNoiseX.fractal2(x / warpScale, z / warpScale, 3);
    const double warpedZ = z + warpAmplitude * m_warpNoiseZ.fractal2(x / warpScale, z / warpScale, 3);
    const double climateX = warpedX + climateWarpAmplitude * m_climateWarpNoiseX.fractal2(x / climateWarpScale, z / climateWarpScale, 3);
    const double climateZ = warpedZ + climateWarpAmplitude * m_climateWarpNoiseZ.fractal2(x / climateWarpScale, z / climateWarpScale, 3);
    const double temperature = std::clamp(
        0.5 + climateContrast * m_temperatureNoise.fractal2(climateX / temperatureScale, climateZ / temperatureScale, 3), 0.0, 1.0);
    const double humidity =
        std::clamp(0.5 + climateContrast * m_humidityNoise.fractal2(climateX / humidityScale, climateZ / humidityScale, 3), 0.0, 1.0);
    const double ruggedness = std::clamp(
        1.0 - ruggednessSharpness * std::abs(m_ruggednessNoise.fractal2(climateX / ruggednessScale, climateZ / ruggednessScale, 3)),
        0.0, 1.0);

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

    // Inverse-distance weighting in climate space blends heights smoothly across biome borders.
    double weightSum = 0.0;
    double landHeight = 0.0;
    double overhang = 0.0;
    double bestWeight = -1.0;
    Column result;
    for (qsizetype i = 0; i < m_definitions.biomes.size(); ++i) {
        const Biome &biome = m_definitions.biomes[i];
        const double dt = temperature - biome.temperature;
        const double dh = humidity - biome.humidity;
        const double dr = ruggednessWeight * (ruggedness - biome.ruggedness);
        const double distanceSquared = dt * dt + dh * dh + dr * dr + 0.0015;
        const double weight = 1.0 / (distanceSquared * distanceSquared);
        weightSum += weight;
        landHeight += weight * biomeHeight(biome, rolling, ridge, terraces);
        overhang += weight * biome.overhang;
        if (weight > bestWeight) {
            bestWeight = weight;
            result.biome = int(i);
        }
    }
    landHeight /= weightSum;

    const double continent = 1.4 * m_continentNoise.fractal2(warpedX / continentScale, warpedZ / continentScale, 4);
    result.landFactor = smoothstep(-0.30, -0.05, continent);
    const double oceanFloor = seaLevel() - oceanDepth + 5.0 * detail;
    const double height = oceanFloor + (landHeight - oceanFloor) * result.landFactor;
    result.height = std::clamp(int(std::lround(height)), 4, QcWorld::worldHeight - 8);
    // Overhangs fade out towards the coast and never reach down to the sea level, so no hollow
    // lies next to or under water.
    const double overhangAmplitude = std::min(overhang / weightSum * result.landFactor, double(result.height - seaLevel() - 3));
    result.overhang = overhangAmplitude < 1.0 ? 0.0 : overhangAmplitude;
    return result;
}

QcBlockId QcTerrainGenerator::surfaceBlockAt(int x, int z) const
{
    Column info = column(x, z);
    if (m_definitions.biomes[info.biome].steep != qcAirBlockId) {
        info.slope = std::max({std::abs(column(x - 1, z).height - info.height), std::abs(column(x + 1, z).height - info.height),
                               std::abs(column(x, z - 1).height - info.height), std::abs(column(x, z + 1).height - info.height)});
    }
    return surfaceBlock(info, x, z);
}

QcBlockId QcTerrainGenerator::surfaceBlock(const Column &column, int x, int z) const
{
    const Biome &biome = m_definitions.biomes[column.biome];
    const int top = column.height - 1;
    if (top < seaLevel() - 1) {
        return biome.underwater;
    }
    // Beaches only where land meets the sea, not on low inland plains.
    if (top <= seaLevel() + 1 && column.landFactor < 1.0) {
        return biome.beach;
    }
    if (biome.snowLine > 0
        && top >= biome.snowLine - snowLineJitter + int(qcPositionRandom(m_seed, x, 7, z) * (2 * snowLineJitter + 1))) {
        return biome.snow;
    }
    if (biome.steep != qcAirBlockId && column.slope >= biome.steepSlope) {
        return biome.steep;
    }
    return biome.surface;
}

QcBlockId QcTerrainGenerator::subsurfaceBlock(const Column &column, QcBlockId surface) const
{
    const Biome &biome = m_definitions.biomes[column.biome];
    if (column.height - 1 < seaLevel() - 1) {
        return biome.underwater;
    }
    if (surface == biome.beach || (surface == biome.steep && biome.steep != qcAirBlockId)) {
        return surface;
    }
    return biome.subsurface;
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

int QcTerrainGenerator::overhangBand(int x, int z, const Column &column, std::array<bool, maxOverhangBand> &solid, int &size) const
{
    const double amplitude = column.overhang;
    const int bottom = std::max(1, int(std::floor(column.height - amplitude)));
    const int end = std::min(QcWorld::worldHeight, int(std::ceil(column.height + amplitude)) + 1);
    size = std::min(end - bottom, maxOverhangBand);

    // A 3D noise reshapes the ground around the surface. Below the plain height it cuts hollows
    // under solid ground (overhanging cliffs, ledges, arches); above it, it only piles up blocks
    // without gaps, so nothing floats.
    bool stacked = true;
    for (int i = 0; i < size; ++i) {
        const int y = bottom + i;
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
    return bottom;
}

int QcTerrainGenerator::groundTop(int x, int z, const Column &column) const
{
    if (column.overhang < 1.0) {
        return column.height - 1;
    }

    std::array<bool, maxOverhangBand> solid{};
    int size = 0;
    const int bottom = overhangBand(x, z, column, solid, size);
    for (int i = size - 1; i >= 0; --i) {
        if (solid[std::size_t(i)]) {
            return bottom + i;
        }
    }
    return bottom - 1;
}

void QcTerrainGenerator::fillColumn(QcChunk &chunk, int localX, int localZ, const Column &column) const
{
    const Biome &biome = m_definitions.biomes[column.biome];
    const QcBlockId stone = m_definitions.stone;
    const QcBlockId bedrock = m_definitions.bedrock;
    const int bedrockLayers = m_definitions.bedrockLayers;
    const int worldX = chunk.pos().x * QcWorld::chunkSize + localX;
    const int worldZ = chunk.pos().z * QcWorld::chunkSize + localZ;
    const QcBlockId surface = surfaceBlock(column, worldX, worldZ);
    const QcBlockId subsurface = subsurfaceBlock(column, surface);
    std::array<bool, maxOverhangBand> band{};
    int bandSize = 0;
    const bool overhangs = column.overhang >= 1.0;
    const int bandBottom = overhangs ? overhangBand(worldX, worldZ, column, band, bandSize) : column.height;
    const int highest = overhangs ? bandBottom + bandSize - 1 : column.height - 1;
    const auto solidAt = [&](int y) { return y < bandBottom || (y - bandBottom < bandSize && band[std::size_t(y - bandBottom)]); };

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

    for (int y = column.height; y < seaLevel(); ++y) {
        chunk.setBlock(localX, y, localZ, y == seaLevel() - 1 ? biome.frozenWater : m_definitions.water);
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

void QcTerrainGenerator::growVein(const Ore &ore, int vein, QcChunkPos pos, QList<QcBlockPos> &cells) const
{
    constexpr std::array<std::array<int, 3>, 6> directions{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    const auto random = [&](int salt) { return qcPositionRandom(ore.seed, pos.x, vein * 1024 + salt, pos.z); };

    // A compact clump: each block grows from a random earlier one onto a free face, kept inside
    // this chunk so veins never depend on neighbours.
    cells.clear();
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
}

void QcTerrainGenerator::placeVein(QcChunk &chunk, const Ore &ore, const QList<QcBlockPos> &cells) const
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
    QList<QcBlockPos> cells;
    for (const Ore &ore : m_definitions.ores) {
        for (int vein = 0; vein < ore.veins; ++vein) {
            growVein(ore, vein, pos, cells);
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
    const int baseX = chunk.pos().x * QcWorld::chunkSize;
    const int baseZ = chunk.pos().z * QcWorld::chunkSize;
    QcTreeShape::build(feature.shape, base, random, m_seed, [&](QcBlockPos pos, QcBlockId block, bool onlyIntoAir) {
        const int lx = pos.x - baseX;
        const int lz = pos.z - baseZ;
        if (lx < 0 || lx >= QcWorld::chunkSize || lz < 0 || lz >= QcWorld::chunkSize || pos.y < 0 || pos.y >= QcWorld::worldHeight) {
            return;
        }
        if (onlyIntoAir && chunk.block(lx, pos.y, lz) != qcAirBlockId) {
            return;
        }
        chunk.setBlock(lx, pos.y, lz, block);
    });
}

std::optional<int> QcTerrainGenerator::chosenFeature(const Column &column, int x, int z, bool shore) const
{
    for (const auto &[featureIndex, density] : m_definitions.biomes[column.biome].features) {
        const Feature &feature = m_definitions.features[featureIndex];
        if (feature.nearWater != shore) {
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
    if (m_definitions.biomes[info.biome].features.isEmpty() || info.height - 1 < seaLevel()) {
        return;
    }

    // Ground at sea level only takes features that want water next to them, and only where a
    // neighbouring column is under water.
    const bool shore = info.height - 1 == seaLevel();
    const std::optional<int> featureIndex = chosenFeature(info, x, z, shore);
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
    const QcBlockId ground = surfaceBlock(info, x, z);
    if (!feature.ground.isEmpty() && !feature.ground.contains(ground)) {
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

    placeFeature(chunk, feature, {x, top + 1, z}, qcPositionRandom(feature.seed, x, 1, z));
}

void QcTerrainGenerator::placeFeatures(QcChunk &chunk, const QcTerrainColumnGrid &columns, const QList<bool> &carved,
                                       const QcVillagePlan *village, const QList<QcSurfacePlanner::Rect> &structures) const
{
    const int baseX = chunk.pos().x * QcWorld::chunkSize;
    const int baseZ = chunk.pos().z * QcWorld::chunkSize;
    for (int z = baseZ - featureMargin; z < baseZ + QcWorld::chunkSize + featureMargin; ++z) {
        for (int x = baseX - featureMargin; x < baseX + QcWorld::chunkSize + featureMargin; ++x) {
            // Trees reach featureMargin blocks from their trunk: none grows into a village.
            if (village && village->covers(x, z, featureMargin)) {
                continue;
            }
            // Nor into a surface structure, where it would be cut or left floating.
            const QcSurfacePlanner::Rect trunk{x, z, x, z};
            if (std::ranges::any_of(structures, [&](QcSurfacePlanner::Rect footprint) { return footprint.intersects(trunk, featureMargin); })) {
                continue;
            }
            placeFeatureInColumn(chunk, columns, carved, x, z);
        }
    }
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
    // near a surface structure either (decision E-630).
    const std::shared_ptr<const QcVillagePlan> village = m_villages->planFor(pos);
    const QList<QcSurfacePlanner::Rect> structures = m_surface->footprintsNear(pos, featureMargin);
    placeFeatures(*chunk, columns, carved, village.get(), structures);
    if ((!village || !village->overlaps(pos, featureMargin)) && structures.isEmpty()) {
        m_pools.placeOnSurface(*chunk, columns, *this);
    }
    // Mine shaft entrances over the trees, cacti and pools (none lies near a village).
    m_mineshafts->buildEntrances(*chunk);
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

bool QcTerrainGenerator::fitsSpawn(int x, int z, const Column &column, std::shared_ptr<QcChunk> &chunk) const
{
    const bool candidate =
        column.landFactor > 0.99 && column.height > seaLevel() + 2 && column.height < seaLevel() + 30 && column.overhang < 1.0;
    if (!candidate || isCave(x, column.height - 1, z)) {
        return false;
    }

    const QcBlockPos pos{x, column.height - 1, z};
    if (!chunk || chunk->pos() != pos.chunk()) {
        chunk = generate(pos.chunk());
    }
    return chunk->highestNonAir(pos.localX(), pos.localZ()) == pos.y
           && isNaturalGround(chunk->block(pos.localX(), pos.y, pos.localZ()));
}

std::optional<QcBlockPos> QcTerrainGenerator::spawnOnRing(int radius, std::shared_ptr<QcChunk> &chunk) const
{
    for (int dx = -radius; dx <= radius; dx += 8) {
        for (int dz = -radius; dz <= radius; dz += 8) {
            if (std::max(std::abs(dx), std::abs(dz)) != radius) {
                continue;
            }
            const Column info = column(dx, dz);
            if (fitsSpawn(dx, dz, info, chunk)) {
                return QcBlockPos{dx, info.height, dz};
            }
        }
    }
    return std::nullopt;
}

QcBlockPos QcTerrainGenerator::spawnPoint() const
{
    // Nearest dry, not too high open ground on a spiral of growing squares around the origin: no
    // tree crown, cave or plant above the surface block, and no overhanging mountain land.
    std::shared_ptr<QcChunk> chunk;
    for (int radius = 0; radius <= spawnSearchRadius; radius += 8) {
        if (const std::optional<QcBlockPos> spawn = spawnOnRing(radius, chunk)) {
            return *spawn;
        }
    }
    return {0, std::max(column(0, 0).height, seaLevel()), 0};
}
