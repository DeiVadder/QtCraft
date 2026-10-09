#include "QcTerrainGenerator.h"

#include "QcTreeShape.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

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
// Terraces form only in patches of about this size.
constexpr double terraceScale = 380.0;
// Spreads the climate noise (about +-0.3 around 0) over most of [0, 1].
constexpr double climateContrast = 1.5;
// Ruggedness only rises where the ruggedness noise is near zero: long winding chains.
constexpr double ruggednessSharpness = 8.0;
// The ruggedness axis counts twice as much as temperature and humidity.
constexpr double ruggednessWeight = 2.0;
constexpr double tunnelHorizontalScale = 56.0;
constexpr double tunnelVerticalScale = 36.0;
constexpr double cavernHorizontalScale = 90.0;
constexpr double cavernVerticalScale = 50.0;
// Tunnel width changes slowly along a tunnel: thin noodles to wide passages.
constexpr double tunnelWidthHorizontalScale = 96.0;
constexpr double tunnelWidthVerticalScale = 48.0;
// Size of the patches where caves may open to the surface.
constexpr double entranceScale = 80.0;
// Caverns stay deep underground.
constexpr int cavernMaxY = 50;
// Ocean floor lies this far below sea level (before detail noise).
constexpr double oceanDepth = 12.0;
// Cave noise is evaluated on a lattice with this spacing and interpolated in between.
constexpr int caveLattice = 4;
constexpr int latticeColumns = QcWorld::chunkSize / caveLattice + 1;
constexpr int latticeRows = QcWorld::worldHeight / caveLattice + 1;
// Trees reach this far from their trunk, so neighbouring columns are scanned for features.
constexpr int featureMargin = 3;
// Columns computed around a chunk: features plus one more for their water and slope checks.
constexpr int gridMargin = featureMargin + 1;
constexpr int gridSize = QcWorld::chunkSize + 2 * gridMargin;
// The snow line is ragged by this many blocks.
constexpr int snowLineJitter = 3;
constexpr int spawnSearchRadius = 512;

double smoothstep(double edge0, double edge1, double x)
{
    const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double trilinear(const std::array<double, 8> &corners, double fx, double fy, double fz)
{
    const double x00 = corners[0] + (corners[1] - corners[0]) * fx;
    const double x10 = corners[2] + (corners[3] - corners[2]) * fx;
    const double x01 = corners[4] + (corners[5] - corners[4]) * fx;
    const double x11 = corners[6] + (corners[7] - corners[6]) * fx;
    const double y0 = x00 + (x10 - x00) * fy;
    const double y1 = x01 + (x11 - x01) * fy;
    return y0 + (y1 - y0) * fz;
}

enum CaveField { TunnelA, TunnelB, Cavern, TunnelWidth, CaveFieldCount };
static_assert(CaveFieldCount == 4, "carves() takes four cave fields");

// Terraces: flat steps of `step` blocks with a cliff at the end of each.
double terraced(double height, int step)
{
    const double level = height / step;
    const double floor = std::floor(level);
    return (floor + smoothstep(0.97, 1.0, level - floor)) * step;
}

} // namespace

class QcTerrainGenerator::ColumnGrid
{
public:
    ColumnGrid(const QcTerrainGenerator &generator, QcChunkPos pos)
        : m_baseX(pos.x * QcWorld::chunkSize - gridMargin)
        , m_baseZ(pos.z * QcWorld::chunkSize - gridMargin)
    {
        m_columns.reserve(qsizetype(gridSize) * gridSize);
        for (int z = 0; z < gridSize; ++z) {
            for (int x = 0; x < gridSize; ++x) {
                m_columns.append(generator.column(m_baseX + x, m_baseZ + z));
            }
        }
        // Slopes inside the outermost ring; the ring itself is only a neighbour.
        for (int z = 1; z < gridSize - 1; ++z) {
            for (int x = 1; x < gridSize - 1; ++x) {
                Column &column = m_columns[z * gridSize + x];
                const int height = column.height;
                column.slope = std::max({std::abs(m_columns[z * gridSize + x - 1].height - height),
                                         std::abs(m_columns[z * gridSize + x + 1].height - height),
                                         std::abs(m_columns[(z - 1) * gridSize + x].height - height),
                                         std::abs(m_columns[(z + 1) * gridSize + x].height - height)});
            }
        }
    }

    // World coordinates within the chunk plus `gridMargin`.
    const Column &at(int x, int z) const { return m_columns[(z - m_baseZ) * gridSize + (x - m_baseX)]; }
    bool contains(int x, int z) const
    {
        return x >= m_baseX && x < m_baseX + gridSize && z >= m_baseZ && z < m_baseZ + gridSize;
    }

private:
    int m_baseX = 0;
    int m_baseZ = 0;
    QList<Column> m_columns;
};

struct QcTerrainGenerator::Feature
{
    QcTreeShape::Params shape;
    QSet<QcBlockId> ground;
    bool nearWater = false;
    int patchSize = 0;
    QcNoise patches{0};
};

struct QcTerrainGenerator::Biome
{
    QString name;
    double temperature = 0.5;
    double humidity = 0.5;
    double ruggedness = 0.2;
    double baseHeight = 68.0;
    double heightVariation = 6.0;
    double ridges = 0.0;
    int terraceHeight = 0;
    int overhang = 0;
    QcBlockId steep = qcAirBlockId;
    int steepSlope = 3;
    QcBlockId surface = qcAirBlockId;
    QcBlockId subsurface = qcAirBlockId;
    int subsurfaceDepth = 3;
    QcBlockId underwater = qcAirBlockId;
    QcBlockId beach = qcAirBlockId;
    QcBlockId frozenWater = qcAirBlockId;
    int snowLine = 0;
    QcBlockId snow = qcAirBlockId;
    // Index into m_features and density per column.
    QList<std::pair<int, double>> features;
};

struct QcTerrainGenerator::Pool
{
    QcBlockId liquid = qcAirBlockId;
    int minY = 8;
    int maxY = 40;
    int attempts = 1;
    double chance = 0.0;
    int radius = 3;
    int depth = 2;
    quint64 seed = 0;
    bool surface = false;
    // Biome indices it may lie in; empty for all.
    QSet<int> biomes;
};

struct QcTerrainGenerator::Ore
{
    QcBlockId block = qcAirBlockId;
    QcBlockId replaces = qcAirBlockId;
    int minY = 1;
    int maxY = 64;
    int veins = 8;
    int veinSize = 6;
    quint64 seed = 0;
};

QcTerrainGenerator::QcTerrainGenerator(std::shared_ptr<const QcContentRegistry> content, qint64 seed, QStringList *warnings)
    : m_content(std::move(content))
    , m_seed(quint64(seed))
    , m_continentNoise(qcMixSeed(quint64(seed), 1))
    , m_detailNoise(qcMixSeed(quint64(seed), 2))
    , m_temperatureNoise(qcMixSeed(quint64(seed), 3))
    , m_humidityNoise(qcMixSeed(quint64(seed), 4))
    , m_tunnelNoiseA(qcMixSeed(quint64(seed), 5))
    , m_tunnelNoiseB(qcMixSeed(quint64(seed), 6))
    , m_cavernNoise(qcMixSeed(quint64(seed), 7))
    , m_ruggednessNoise(qcMixSeed(quint64(seed), 8))
    , m_warpNoiseX(qcMixSeed(quint64(seed), 9))
    , m_warpNoiseZ(qcMixSeed(quint64(seed), 10))
    , m_hillNoise(qcMixSeed(quint64(seed), 11))
    , m_ridgeNoise(qcMixSeed(quint64(seed), 12))
    , m_terraceNoise(qcMixSeed(quint64(seed), 13))
    , m_tunnelWidthNoise(qcMixSeed(quint64(seed), 14))
    , m_entranceNoise(qcMixSeed(quint64(seed), 15))
    , m_overhangNoise(qcMixSeed(quint64(seed), 16))
{
    const QcTerrainSettings &terrain = m_content->terrainSettings();
    m_seaLevel = terrain.seaLevel;
    m_stone = resolve(terrain.stoneBlock, qcAirBlockId, warnings);
    m_water = resolve(terrain.waterBlock, qcAirBlockId, warnings);
    m_bedrock = terrain.bedrockBlock.isEmpty() ? m_stone : resolve(terrain.bedrockBlock, m_stone, warnings);
    m_caves = terrain.caves;
    m_tunnelWidth = terrain.caveTunnelWidth;
    m_cavernThreshold = terrain.caveCavernThreshold;
    m_caveLiquid = terrain.caveLiquidBlock.isEmpty() ? qcAirBlockId : resolve(terrain.caveLiquidBlock, qcAirBlockId, warnings);
    m_caveLiquidLevel = terrain.caveLiquidLevel;
    m_caveRoof = terrain.caveRoof;
    m_bedrockLayers = terrain.bedrockLayers;
    // The entrance threshold is the quantile of the entrance noise that gives the wanted share.
    if (terrain.caveEntrances > 0.0) {
        QList<double> samples;
        constexpr int grid = 64;
        samples.reserve(qsizetype(grid) * grid);
        for (int i = 0; i < grid; ++i) {
            for (int j = 0; j < grid; ++j) {
                samples.append(m_entranceNoise.noise2(i * 1.37, j * 1.37));
            }
        }
        std::ranges::sort(samples);
        const auto index = qsizetype((1.0 - terrain.caveEntrances) * double(samples.size() - 1));
        m_entranceThreshold = terrain.caveEntrances >= 1.0 ? -2.0 : samples[std::clamp<qsizetype>(index, 0, samples.size() - 1)];
    }

    quint64 featureIndex = 0;
    for (const QcFeatureDefinition &definition : m_content->features()) {
        const quint64 poolSeed = qcMixSeed(m_seed, 3000 + featureIndex++);
        if (definition.type == QcFeatureDefinition::Type::Pool) {
            Pool pool;
            pool.liquid = resolve(definition.trunkBlock, qcAirBlockId, warnings);
            pool.minY = std::max(definition.minY, definition.depth + 1);
            pool.maxY = definition.maxY;
            pool.attempts = definition.attempts;
            pool.chance = definition.chance;
            pool.radius = definition.radius;
            pool.depth = definition.depth;
            pool.seed = poolSeed;
            pool.surface = definition.surface;
            for (const QString &biome : definition.biomes) {
                const auto found = std::ranges::find(m_content->biomes(), biome, &QcBiomeDefinition::name);
                if (found != m_content->biomes().end()) {
                    pool.biomes.insert(int(found - m_content->biomes().begin()));
                } else if (warnings) {
                    warnings->append(u"Pool %1 references unknown biome %2"_s.arg(definition.name, biome));
                }
            }
            if (pool.liquid != qcAirBlockId && pool.minY <= pool.maxY) {
                m_pools.append(pool);
            }
        }
        Feature feature;
        feature.shape.type = definition.type;
        feature.shape.trunk = resolve(definition.trunkBlock, qcAirBlockId, warnings);
        feature.shape.leaves =
            definition.leavesBlock.isEmpty() ? qcAirBlockId : resolve(definition.leavesBlock, qcAirBlockId, warnings);
        feature.shape.minHeight = definition.minHeight;
        feature.shape.maxHeight = definition.maxHeight;
        feature.shape.cone = definition.shape == u"cone"_s;
        feature.nearWater = definition.nearWater;
        feature.patchSize = definition.patchSize;
        feature.patches = QcNoise(qcMixSeed(m_seed, 2000 + featureIndex));
        for (const QString &ground : definition.groundBlocks) {
            feature.ground.insert(resolve(ground, qcAirBlockId, warnings));
        }
        m_features.append(feature);
    }

    for (const QcBiomeDefinition &definition : m_content->biomes()) {
        Biome biome;
        biome.name = definition.name;
        biome.temperature = definition.temperature;
        biome.humidity = definition.humidity;
        biome.ruggedness = definition.ruggedness;
        biome.baseHeight = definition.baseHeight;
        biome.heightVariation = definition.heightVariation;
        biome.ridges = definition.ridges;
        biome.terraceHeight = definition.terraceHeight;
        biome.overhang = definition.overhang;
        m_anyOverhangs = m_anyOverhangs || biome.overhang > 0;
        biome.steep = resolve(definition.steepBlock, qcAirBlockId, warnings);
        biome.steepSlope = definition.steepSlope;
        m_anyRidges = m_anyRidges || biome.ridges > 0.0;
        m_anyTerraces = m_anyTerraces || biome.terraceHeight > 0;
        biome.surface = resolve(definition.surfaceBlock, m_stone, warnings);
        biome.subsurface = resolve(definition.subsurfaceBlock, biome.surface, warnings);
        biome.subsurfaceDepth = definition.subsurfaceDepth;
        biome.underwater = resolve(definition.underwaterBlock, biome.subsurface, warnings);
        biome.beach = resolve(definition.beachBlock, biome.surface, warnings);
        biome.frozenWater = definition.frozenWaterBlock.isEmpty() ? m_water : resolve(definition.frozenWaterBlock, m_water, warnings);
        biome.snowLine = definition.snowLine;
        biome.snow = definition.snowBlock.isEmpty() ? biome.surface : resolve(definition.snowBlock, biome.surface, warnings);
        for (const QcFeaturePlacement &placement : definition.features) {
            const auto it = std::ranges::find(m_content->features(), placement.feature, &QcFeatureDefinition::name);
            if (it == m_content->features().end()) {
                if (warnings) {
                    warnings->append(u"Biome %1 references unknown feature %2"_s.arg(definition.name, placement.feature));
                }
                continue;
            }
            if (it->type == QcFeatureDefinition::Type::Pool) {
                if (warnings) {
                    warnings->append(u"Biome %1 lists the pool %2; pools are placed in every chunk"_s.arg(definition.name, placement.feature));
                }
                continue;
            }
            biome.features.append({int(it - m_content->features().begin()), placement.density});
        }
        for (const Biome &other : std::as_const(m_biomes)) {
            if (warnings && other.temperature == biome.temperature && other.humidity == biome.humidity
                && other.ruggedness == biome.ruggedness) {
                warnings->append(u"Biomes %1 and %2 share a climate point; only %1 can appear"_s.arg(other.name, biome.name));
            }
        }
        m_biomes.append(biome);
    }
    if (m_biomes.isEmpty()) {
        // Without any biome data the world is bare stone hills, which keeps the game playable.
        Biome fallback;
        fallback.name = u"qtcraft:fallback"_s;
        fallback.surface = fallback.subsurface = fallback.underwater = fallback.beach = m_stone;
        fallback.frozenWater = fallback.snow = m_stone;
        m_biomes.append(fallback);
        if (warnings) {
            warnings->append(u"No biomes defined; using a stone fallback"_s);
        }
    }

    quint64 oreIndex = 100;
    for (const QcOreDefinition &definition : m_content->ores()) {
        Ore ore;
        ore.block = resolve(definition.block, qcAirBlockId, warnings);
        ore.replaces = resolve(definition.replaces, m_stone, warnings);
        ore.minY = definition.minY;
        ore.maxY = definition.maxY;
        ore.veins = definition.veinsPerChunk;
        ore.veinSize = definition.veinSize;
        ore.seed = qcMixSeed(m_seed, oreIndex++);
        if (ore.block != qcAirBlockId) {
            m_ores.append(ore);
        }
    }
}

QcTerrainGenerator::~QcTerrainGenerator() = default;

QcBlockId QcTerrainGenerator::resolve(const QString &name, QcBlockId fallback, QStringList *warnings) const
{
    // An omitted optional block simply uses the fallback.
    if (name.isEmpty()) {
        return fallback;
    }
    const std::optional<QcBlockId> id = m_content->blockId(name);
    if (!id) {
        if (warnings) {
            warnings->append(u"World generation references unknown block '%1'"_s.arg(name));
        }
        return fallback;
    }
    return *id;
}

QString QcTerrainGenerator::biomeName(int index) const
{
    return index >= 0 && index < m_biomes.size() ? m_biomes[index].name : QString();
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
    const double temperature = std::clamp(
        0.5 + climateContrast * m_temperatureNoise.fractal2(warpedX / temperatureScale, warpedZ / temperatureScale, 3), 0.0, 1.0);
    const double humidity =
        std::clamp(0.5 + climateContrast * m_humidityNoise.fractal2(warpedX / humidityScale, warpedZ / humidityScale, 3), 0.0, 1.0);
    const double ruggedness = std::clamp(
        1.0 - ruggednessSharpness * std::abs(m_ruggednessNoise.fractal2(warpedX / ruggednessScale, warpedZ / ruggednessScale, 3)),
        0.0, 1.0);

    // Rolling hills: broad swells with smaller hills on top, in [-1, 1].
    const double detail = std::clamp(1.6 * m_detailNoise.fractal2(x / detailScale, z / detailScale, 5), -1.0, 1.0);
    const double rolling = std::clamp(0.75 * detail + 0.6 * m_hillNoise.fractal2(x / hillScale, z / hillScale, 3), -1.0, 1.0);
    // Ridges: sharp crests where the noise crosses zero, in about [-0.6, 1]; the valleys between
    // them stay above the sea.
    double ridge = 0.0;
    if (m_anyRidges) {
        const double crest = 1.0 - std::min(1.0, 2.2 * std::abs(m_ridgeNoise.fractal2(x / ridgeScale, z / ridgeScale, 4)));
        ridge = 1.6 * crest * crest - 0.6 + 0.2 * detail;
    }
    const double terraces =
        m_anyTerraces ? smoothstep(-0.1, 0.15, m_terraceNoise.fractal2(x / terraceScale, z / terraceScale, 2)) : 0.0;

    // Inverse-distance weighting in climate space blends heights smoothly across biome borders.
    double weightSum = 0.0;
    double landHeight = 0.0;
    double overhang = 0.0;
    double bestWeight = -1.0;
    Column result;
    for (qsizetype i = 0; i < m_biomes.size(); ++i) {
        const Biome &biome = m_biomes[i];
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
    const double oceanFloor = m_seaLevel - oceanDepth + 5.0 * detail;
    const double height = oceanFloor + (landHeight - oceanFloor) * result.landFactor;
    result.height = std::clamp(int(std::lround(height)), 4, QcWorld::worldHeight - 8);
    // Overhangs fade out towards the coast and never reach down to the sea level, so no hollow
    // lies next to or under water.
    const double overhangAmplitude = std::min(overhang / weightSum * result.landFactor, double(result.height - m_seaLevel - 3));
    result.overhang = overhangAmplitude < 1.0 ? 0.0 : overhangAmplitude;
    return result;
}

QcBlockId QcTerrainGenerator::surfaceBlockAt(int x, int z) const
{
    Column info = column(x, z);
    if (m_biomes[info.biome].steep != qcAirBlockId) {
        info.slope = std::max({std::abs(column(x - 1, z).height - info.height), std::abs(column(x + 1, z).height - info.height),
                               std::abs(column(x, z - 1).height - info.height), std::abs(column(x, z + 1).height - info.height)});
    }
    return surfaceBlock(info, x, z);
}

QcBlockId QcTerrainGenerator::surfaceBlock(const Column &column, int x, int z) const
{
    const Biome &biome = m_biomes[column.biome];
    const int top = column.height - 1;
    if (top < m_seaLevel - 1) {
        return biome.underwater;
    }
    // Beaches only where land meets the sea, not on low inland plains.
    if (top <= m_seaLevel + 1 && column.landFactor < 1.0) {
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
    const Biome &biome = m_biomes[column.biome];
    if (column.height - 1 < m_seaLevel - 1) {
        return biome.underwater;
    }
    if (surface == biome.beach || (surface == biome.steep && biome.steep != qcAirBlockId)) {
        return surface;
    }
    return biome.subsurface;
}

double QcTerrainGenerator::caveDensity(int field, int x, int y, int z) const
{
    switch (field) {
    case TunnelA:
        return m_tunnelNoiseA.noise3(x / tunnelHorizontalScale, y / tunnelVerticalScale, z / tunnelHorizontalScale);
    case TunnelB:
        return m_tunnelNoiseB.noise3(x / tunnelHorizontalScale, y / tunnelVerticalScale, z / tunnelHorizontalScale);
    case TunnelWidth:
        return m_tunnelWidthNoise.noise3(x / tunnelWidthHorizontalScale, y / tunnelWidthVerticalScale, z / tunnelWidthHorizontalScale);
    default:
        return m_cavernNoise.fractal3(x / cavernHorizontalScale, y / cavernVerticalScale, z / cavernHorizontalScale, 2);
    }
}

bool QcTerrainGenerator::carves(const std::array<double, 4> &values, int y) const
{
    // Tunnels: where two noise fields are both near zero, a winding line; its width follows a
    // third, slow field from thin noodles to wide passages.
    const double width = m_tunnelWidth * (0.45 + 1.1 * smoothstep(-0.35, 0.35, values[TunnelWidth]));
    const bool tunnel = std::abs(values[TunnelA]) < width && std::abs(values[TunnelB]) < width;
    // Caverns with pillars, fading out above the bedrock and towards their upper limit.
    const double threshold = m_cavernThreshold + 0.35 * (1.0 - smoothstep(3.0, 12.0, y))
                             + 0.35 * smoothstep(cavernMaxY - 16.0, double(cavernMaxY), y);
    const bool cavern = y < cavernMaxY && values[Cavern] > threshold;
    return tunnel || cavern;
}

int QcTerrainGenerator::caveTop(const Column &column, int x, int z) const
{
    // Keep a solid lid under seas, lakes and beaches so water never pours into caves.
    if (column.height <= m_seaLevel + 1) {
        return column.height - 6;
    }
    const bool entrance = m_entranceNoise.noise2(x / entranceScale, z / entranceScale) > m_entranceThreshold;
    return entrance ? column.height - 1 : column.height - 1 - m_caveRoof;
}

bool QcTerrainGenerator::isCave(int x, int y, int z) const
{
    if (!m_caves || y <= 0 || y > caveTop(column(x, z), x, z)) {
        return false;
    }
    const int lx = qcFloorDiv(x, caveLattice) * caveLattice;
    const int ly = qcFloorDiv(y, caveLattice) * caveLattice;
    const int lz = qcFloorDiv(z, caveLattice) * caveLattice;
    const double fx = double(x - lx) / caveLattice;
    const double fy = double(y - ly) / caveLattice;
    const double fz = double(z - lz) / caveLattice;
    std::array<double, CaveFieldCount> values{};
    for (int field = 0; field < CaveFieldCount; ++field) {
        std::array<double, 8> corners{};
        for (int corner = 0; corner < 8; ++corner) {
            corners[std::size_t(corner)] = caveDensity(field, lx + (corner & 1) * caveLattice,
                                                       ly + ((corner >> 1) & 1) * caveLattice,
                                                       lz + ((corner >> 2) & 1) * caveLattice);
        }
        values[std::size_t(field)] = trilinear(corners, fx, fy, fz);
    }
    return carves(values, y);
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
    const Biome &biome = m_biomes[column.biome];
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
        QcBlockId block = m_stone;
        if (depth == 0) {
            block = surface;
        } else if (depth <= biome.subsurfaceDepth) {
            block = subsurface;
        }
        // Sand and gravel would fall into the hollows of an overhang band: rock instead.
        if (overhangs && block != m_stone && m_content->isValidBlock(block) && m_content->block(block).falls) {
            block = m_stone;
        }
        // Solid at the bottom, thinning out over the layers above.
        if (y == 0 || (y < m_bedrockLayers && qcPositionRandom(m_seed, worldX, y, worldZ) < double(m_bedrockLayers - y) / m_bedrockLayers)) {
            block = m_bedrock;
        }
        chunk.setBlock(localX, y, localZ, block);
    }
    for (int y = column.height; y < m_seaLevel; ++y) {
        chunk.setBlock(localX, y, localZ, y == m_seaLevel - 1 ? biome.frozenWater : m_water);
    }
}

void QcTerrainGenerator::carveCaves(QcChunk &chunk, const ColumnGrid &columns) const
{
    if (!m_caves) {
        return;
    }
    const int baseX = chunk.pos().x * QcWorld::chunkSize;
    const int baseZ = chunk.pos().z * QcWorld::chunkSize;
    int maxHeight = 0;
    for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
        for (int lx = 0; lx < QcWorld::chunkSize; ++lx) {
            maxHeight = std::max(maxHeight, columns.at(baseX + lx, baseZ + lz).height);
        }
    }
    const int rows = std::min(latticeRows, maxHeight / caveLattice + 2);

    // Lattice values for this chunk; the same corners and interpolation as isCave(), so both agree.
    QList<double> lattice(qsizetype(CaveFieldCount) * latticeColumns * latticeColumns * rows);
    const auto at = [rows](int field, int ix, int iy, int iz) {
        return ((qsizetype(field) * latticeColumns + iz) * latticeColumns + ix) * rows + iy;
    };
    for (int field = 0; field < CaveFieldCount; ++field) {
        for (int iz = 0; iz < latticeColumns; ++iz) {
            for (int ix = 0; ix < latticeColumns; ++ix) {
                for (int iy = 0; iy < rows; ++iy) {
                    lattice[at(field, ix, iy, iz)] =
                        caveDensity(field, baseX + ix * caveLattice, iy * caveLattice, baseZ + iz * caveLattice);
                }
            }
        }
    }

    for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
        for (int lx = 0; lx < QcWorld::chunkSize; ++lx) {
            const int top = caveTop(columns.at(baseX + lx, baseZ + lz), baseX + lx, baseZ + lz);
            const int ix = lx / caveLattice;
            const int iz = lz / caveLattice;
            const double fx = double(lx % caveLattice) / caveLattice;
            const double fz = double(lz % caveLattice) / caveLattice;
            for (int y = 1; y <= top; ++y) {
                const int iy = y / caveLattice;
                const double fy = double(y % caveLattice) / caveLattice;
                std::array<double, CaveFieldCount> values{};
                for (int field = 0; field < CaveFieldCount; ++field) {
                    std::array<double, 8> corners{};
                    for (int corner = 0; corner < 8; ++corner) {
                        corners[std::size_t(corner)] =
                            lattice[at(field, ix + (corner & 1), iy + ((corner >> 1) & 1), iz + ((corner >> 2) & 1))];
                    }
                    values[std::size_t(field)] = trilinear(corners, fx, fy, fz);
                }
                if (carves(values, y) && chunk.block(lx, y, lz) != m_bedrock) {
                    // Deep down the caves hold lava lakes.
                    chunk.setBlock(lx, y, lz, y <= m_caveLiquidLevel ? m_caveLiquid : qcAirBlockId);
                }
            }
        }
    }
}

void QcTerrainGenerator::placeOres(QcChunk &chunk) const
{
    constexpr std::array<std::array<int, 3>, 6> directions{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    const QcChunkPos pos = chunk.pos();
    QList<QcBlockPos> cells;
    for (const Ore &ore : m_ores) {
        for (int vein = 0; vein < ore.veins; ++vein) {
            const auto random = [&](int salt) { return qcPositionRandom(ore.seed, pos.x, vein * 1024 + salt, pos.z); };
            // A compact clump: each block grows from a random earlier one onto a free face, kept
            // inside this chunk so veins never depend on neighbours.
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
            for (const QcBlockPos &cell : std::as_const(cells)) {
                if (cell.y >= 0 && cell.y < QcWorld::worldHeight && chunk.block(cell.x, cell.y, cell.z) == ore.replaces) {
                    chunk.setBlock(cell.x, cell.y, cell.z, ore.block);
                }
            }
        }
    }
}

bool QcTerrainGenerator::holdsLiquid(QcBlockId block) const
{
    if (block == qcAirBlockId || !m_content->isValidBlock(block)) {
        return false;
    }
    const QcBlockDefinition &definition = m_content->block(block);
    return definition.solid && !definition.liquid && !definition.replaceable;
}

void QcTerrainGenerator::placePools(QcChunk &chunk) const
{
    const QcChunkPos pos = chunk.pos();
    for (const Pool &pool : m_pools) {
        for (int attempt = 0; attempt < pool.attempts && !pool.surface; ++attempt) {
            const auto random = [&](int salt) { return qcPositionRandom(pool.seed, pos.x, attempt * 8 + salt, pos.z); };
            if (random(0) >= pool.chance) {
                continue;
            }
            // Cells on the chunk's edge never become liquid (their neighbours lie in other chunks).
            const int x = 1 + int(random(1) * (QcWorld::chunkSize - 2));
            const int z = 1 + int(random(2) * (QcWorld::chunkSize - 2));
            const int startY = pool.minY + int(random(3) * (pool.maxY - pool.minY + 1));
            // The nearest cave floor at or below the start: air standing on a block that holds liquid.
            for (int y = startY; y >= pool.minY; --y) {
                if (chunk.block(x, y, z) == qcAirBlockId && holdsLiquid(chunk.block(x, y - 1, z))) {
                    sinkPool(chunk, pool, x, y, z, attempt);
                    break;
                }
            }
        }
    }
}

void QcTerrainGenerator::placeSurfacePools(QcChunk &chunk, const ColumnGrid &columns) const
{
    const QcChunkPos pos = chunk.pos();
    for (const Pool &pool : m_pools) {
        for (int attempt = 0; attempt < pool.attempts && pool.surface; ++attempt) {
            const auto random = [&](int salt) { return qcPositionRandom(pool.seed, pos.x, attempt * 8 + salt, pos.z); };
            if (random(0) >= pool.chance) {
                continue;
            }
            // Away from the chunk's edge, on dry ground of an allowed biome under open sky.
            const int x = 2 + int(random(1) * (QcWorld::chunkSize - 4));
            const int z = 2 + int(random(2) * (QcWorld::chunkSize - 4));
            const Column &column = columns.at(pos.x * QcWorld::chunkSize + x, pos.z * QcWorld::chunkSize + z);
            const int top = groundTop(pos.x * QcWorld::chunkSize + x, pos.z * QcWorld::chunkSize + z, column);
            if (top < m_seaLevel || top + 1 >= QcWorld::worldHeight || (!pool.biomes.isEmpty() && !pool.biomes.contains(column.biome))
                || !holdsLiquid(chunk.block(x, top, z)) || m_content->block(chunk.block(x, top + 1, z)).solid) {
                continue;
            }
            sinkPool(chunk, pool, x, top + 1, z, attempt);
        }
    }
}

void QcTerrainGenerator::sinkPool(QcChunk &chunk, const Pool &pool, int centreX, int floorY, int centreZ, int attempt) const
{
    constexpr int last = QcWorld::chunkSize - 1;
    const int baseX = chunk.pos().x * QcWorld::chunkSize;
    const int baseZ = chunk.pos().z * QcWorld::chunkSize;
    const double radius = pool.radius - 0.6 * qcPositionRandom(pool.seed, baseX + centreX, floorY + attempt, baseZ + centreZ);
    const auto key = [](int x, int y, int z) { return (y * QcWorld::chunkSize + z) * QcWorld::chunkSize + x; };

    // A bowl below the floor that narrows with depth and has a ragged rim.
    QSet<int> cells;
    for (int layer = 0; layer < pool.depth; ++layer) {
        const int y = floorY - 1 - layer;
        const double layerRadius = radius - layer * 0.9;
        for (int z = std::max(1, centreZ - pool.radius); z <= std::min(last - 1, centreZ + pool.radius); ++z) {
            for (int x = std::max(1, centreX - pool.radius); x <= std::min(last - 1, centreX + pool.radius); ++x) {
                const double distance = std::hypot(x - centreX, z - centreZ)
                                        + 0.8 * qcPositionRandom(pool.seed, baseX + x, y, baseZ + z);
                const QcBlockId block = chunk.block(x, y, z);
                // On the surface only ground is dug out, and the top layer only under open sky.
                const bool fits = pool.surface ? holdsLiquid(block) && (layer > 0 || !m_content->block(chunk.block(x, y + 1, z)).solid)
                                               : block == qcAirBlockId || holdsLiquid(block);
                if (distance <= layerRadius + 0.4 && block != m_bedrock && fits) {
                    cells.insert(key(x, y, z));
                }
            }
        }
    }
    // Liquid must not flow anywhere: drop cells until every remaining one is bounded below and on
    // its sides by other cells or by blocks that hold liquid.
    const auto contained = [&](int x, int y, int z) {
        return cells.contains(key(x, y, z)) || holdsLiquid(chunk.block(x, y, z));
    };
    const auto isLiquid = [&](QcBlockId block) { return m_content->isValidBlock(block) && m_content->block(block).liquid; };
    for (bool changed = true; changed;) {
        changed = false;
        for (auto it = cells.begin(); it != cells.end();) {
            const int x = *it % QcWorld::chunkSize;
            const int z = *it / QcWorld::chunkSize % QcWorld::chunkSize;
            const int y = *it / (QcWorld::chunkSize * QcWorld::chunkSize);
            // Another liquid above (an earlier pool) would meet this one.
            const bool liquidAbove = !cells.contains(key(x, y + 1, z)) && isLiquid(chunk.block(x, y + 1, z));
            if (contained(x - 1, y, z) && contained(x + 1, y, z) && contained(x, y, z - 1) && contained(x, y, z + 1)
                && contained(x, y - 1, z) && !liquidAbove) {
                ++it;
                continue;
            }
            it = cells.erase(it);
            changed = true;
        }
    }
    // Only pools open to the cave above are worth the liquid.
    const bool visible = std::ranges::any_of(cells, [&](int cell) {
        const int x = cell % QcWorld::chunkSize;
        const int z = cell / QcWorld::chunkSize % QcWorld::chunkSize;
        const int y = cell / (QcWorld::chunkSize * QcWorld::chunkSize);
        const QcBlockId above = chunk.block(x, y + 1, z);
        return (above == qcAirBlockId || (pool.surface && !m_content->block(above).solid && !isLiquid(above)))
               && !cells.contains(key(x, y + 1, z));
    });
    if (!visible) {
        return;
    }
    for (const int cell : std::as_const(cells)) {
        const int x = cell % QcWorld::chunkSize;
        const int z = cell / QcWorld::chunkSize % QcWorld::chunkSize;
        const int y = cell / (QcWorld::chunkSize * QcWorld::chunkSize);
        chunk.setBlock(x, y, z, pool.liquid);
        if (pool.surface) {
            // Plants on the dug-out ground go; nothing else stands there.
            const QcBlockId above = chunk.block(x, y + 1, z);
            if (!cells.contains(key(x, y + 1, z)) && above != qcAirBlockId && !m_content->block(above).solid && !isLiquid(above)) {
                chunk.setBlock(x, y + 1, z, qcAirBlockId);
            }
            continue;
        }
        // Where the cave floor beside the start is higher, the rock over the pool is hollowed out up
        // to two blocks above the start's floor, so the pool lies open instead of under a ledge. Rock
        // that holds back another liquid (beside or above it) or carries a falling block stays.
        for (int above = floorY; y == floorY - 1 && above <= floorY + 1; ++above) {
            const QcBlockId block = chunk.block(x, above, z);
            const QcBlockId top = chunk.block(x, above + 1, z);
            const bool holdsSomething = isLiquid(top) || (m_content->isValidBlock(top) && m_content->block(top).falls)
                                        || isLiquid(chunk.block(x - 1, above, z)) || isLiquid(chunk.block(x + 1, above, z))
                                        || isLiquid(chunk.block(x, above, z - 1)) || isLiquid(chunk.block(x, above, z + 1));
            if (block != m_bedrock && holdsLiquid(block) && !holdsSomething) {
                chunk.setBlock(x, above, z, qcAirBlockId);
            }
        }
    }
}

void QcTerrainGenerator::placeFeature(QcChunk &chunk, const Feature &feature, int x, int baseY, int z, double random) const
{
    const int baseX = chunk.pos().x * QcWorld::chunkSize;
    const int baseZ = chunk.pos().z * QcWorld::chunkSize;
    QcTreeShape::build(feature.shape, {x, baseY, z}, random, m_seed, [&](QcBlockPos pos, QcBlockId block, bool onlyIntoAir) {
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

void QcTerrainGenerator::placeFeatures(QcChunk &chunk, const ColumnGrid &columns, const QList<bool> &carved) const
{
    const int baseX = chunk.pos().x * QcWorld::chunkSize;
    const int baseZ = chunk.pos().z * QcWorld::chunkSize;
    for (int z = baseZ - featureMargin; z < baseZ + QcWorld::chunkSize + featureMargin; ++z) {
        for (int x = baseX - featureMargin; x < baseX + QcWorld::chunkSize + featureMargin; ++x) {
            const Column &info = columns.at(x, z);
            const Biome &biome = m_biomes[info.biome];
            if (biome.features.isEmpty() || info.height - 1 < m_seaLevel) {
                continue;
            }
            // Ground at sea level only takes features that want water next to them, and only where
            // a neighbouring column is under water.
            const bool shore = info.height - 1 == m_seaLevel;
            const auto waterBeside = [&] {
                return columns.at(x + 1, z).height - 1 < m_seaLevel || columns.at(x - 1, z).height - 1 < m_seaLevel
                       || columns.at(x, z + 1).height - 1 < m_seaLevel || columns.at(x, z - 1).height - 1 < m_seaLevel;
            };
            for (const auto &[featureIndex, density] : biome.features) {
                const Feature &feature = m_features[featureIndex];
                if (feature.nearWater != shore) {
                    continue;
                }
                double localDensity = density;
                if (feature.patchSize > 0) {
                    // Patches: dense where the noise is high, none where it is low; symmetric around
                    // 1, so the average density stays about the same.
                    const double patch = feature.patches.fractal2(x / double(feature.patchSize), z / double(feature.patchSize), 2);
                    localDensity *= std::clamp(1.0 + 3.0 * patch, 0.0, 2.0);
                }
                const quint64 featureSeed = qcMixSeed(m_seed, 1000 + quint64(featureIndex));
                if (qcPositionRandom(featureSeed, x, 0, z) >= localDensity) {
                    continue;
                }
                const bool inside = x >= baseX && x < baseX + QcWorld::chunkSize && z >= baseZ && z < baseZ + QcWorld::chunkSize;
                // Only trees reach into this chunk from a column outside it.
                if ((shore && !waterBeside()) || (!inside && feature.shape.type != QcFeatureDefinition::Type::Tree)) {
                    break;
                }
                const QcBlockId ground = surfaceBlock(info, x, z);
                if (!feature.ground.isEmpty() && !feature.ground.contains(ground)) {
                    break;
                }
                // Skip columns whose ground was carved away by a cave opening. Inside the chunk the
                // carving itself tells (cheaper than the noise, recorded before pools and features
                // changed blocks); shores keep a lid, so ask the noise like the neighbours do.
                const int top = groundTop(x, z, info);
                const bool open = inside && !shore ? carved[(z - baseZ) * QcWorld::chunkSize + (x - baseX)] : isCave(x, top, z);
                if (open) {
                    break;
                }
                placeFeature(chunk, feature, x, top + 1, z, qcPositionRandom(featureSeed, x, 1, z));
                break;
            }
        }
    }
}

std::shared_ptr<QcChunk> QcTerrainGenerator::generate(QcChunkPos pos) const
{
    auto chunk = std::make_shared<QcChunk>(pos);
    const ColumnGrid columns(*this, pos);
    for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
        for (int lx = 0; lx < QcWorld::chunkSize; ++lx) {
            fillColumn(*chunk, lx, lz, columns.at(pos.x * QcWorld::chunkSize + lx, pos.z * QcWorld::chunkSize + lz));
        }
    }
    carveCaves(*chunk, columns);
    QList<bool> carved(qsizetype(QcWorld::chunkSize) * QcWorld::chunkSize);
    for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
        for (int lx = 0; lx < QcWorld::chunkSize; ++lx) {
            const int worldX = pos.x * QcWorld::chunkSize + lx;
            const int worldZ = pos.z * QcWorld::chunkSize + lz;
            const int top = groundTop(worldX, worldZ, columns.at(worldX, worldZ));
            carved[lz * QcWorld::chunkSize + lx] = top >= 0 && chunk->block(lx, top, lz) == qcAirBlockId;
        }
    }
    placeOres(*chunk);
    placePools(*chunk);
    placeFeatures(*chunk, columns, carved);
    placeSurfacePools(*chunk, columns);
    for (int i = 0; i < QcWorld::sectionCount; ++i) {
        chunk->section(i).compact();
    }
    return chunk;
}

QcBlockPos QcTerrainGenerator::spawnPoint() const
{
    // Nearest dry, not too high open ground on a spiral of growing squares around the origin: no
    // tree crown, cave or plant above the surface block, and no overhanging mountain land.
    std::shared_ptr<QcChunk> chunk;
    const auto open = [&](int x, int z, const Column &info) {
        const QcBlockPos pos{x, info.height - 1, z};
        if (!chunk || chunk->pos() != pos.chunk()) {
            chunk = generate(pos.chunk());
        }
        return chunk->highestNonAir(pos.localX(), pos.localZ()) == pos.y
               && m_content->block(chunk->block(pos.localX(), pos.y, pos.localZ())).solid;
    };
    for (int radius = 0; radius <= spawnSearchRadius; radius += 8) {
        for (int dx = -radius; dx <= radius; dx += 8) {
            for (int dz = -radius; dz <= radius; dz += 8) {
                if (std::max(std::abs(dx), std::abs(dz)) != radius) {
                    continue;
                }
                const Column info = column(dx, dz);
                if (info.landFactor > 0.99 && info.height > m_seaLevel + 2 && info.height < m_seaLevel + 30 && info.overhang < 1.0
                    && !isCave(dx, info.height - 1, dz) && open(dx, dz, info)) {
                    return {dx, info.height, dz};
                }
            }
        }
    }
    return {0, std::max(column(0, 0).height, m_seaLevel), 0};
}
