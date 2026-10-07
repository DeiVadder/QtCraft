#include "QcTerrainGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// Horizontal scales of the noise fields in blocks.
constexpr double continentScale = 900.0;
constexpr double detailScale = 160.0;
constexpr double temperatureScale = 1400.0;
constexpr double humidityScale = 1200.0;
constexpr double tunnelHorizontalScale = 56.0;
constexpr double tunnelVerticalScale = 36.0;
constexpr double cavernHorizontalScale = 90.0;
constexpr double cavernVerticalScale = 50.0;
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

enum CaveField { TunnelA, TunnelB, Cavern, CaveFieldCount };

} // namespace

struct QcTerrainGenerator::Feature
{
    QcFeatureDefinition::Type type = QcFeatureDefinition::Type::Tree;
    QcBlockId trunk = qcAirBlockId;
    QcBlockId leaves = qcAirBlockId;
    int minHeight = 4;
    int maxHeight = 6;
    bool cone = false;
    QSet<QcBlockId> ground;
};

struct QcTerrainGenerator::Biome
{
    QString name;
    double temperature = 0.5;
    double humidity = 0.5;
    double baseHeight = 68.0;
    double heightVariation = 6.0;
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
{
    const QcTerrainSettings &terrain = m_content->terrainSettings();
    m_seaLevel = terrain.seaLevel;
    m_stone = resolve(terrain.stoneBlock, qcAirBlockId, warnings);
    m_water = resolve(terrain.waterBlock, qcAirBlockId, warnings);
    m_bedrock = terrain.bedrockBlock.isEmpty() ? m_stone : resolve(terrain.bedrockBlock, m_stone, warnings);
    m_caves = terrain.caves;
    m_tunnelWidth = terrain.caveTunnelWidth;
    m_cavernThreshold = terrain.caveCavernThreshold;

    for (const QcFeatureDefinition &definition : m_content->features()) {
        Feature feature;
        feature.type = definition.type;
        feature.trunk = resolve(definition.trunkBlock, qcAirBlockId, warnings);
        feature.leaves = definition.leavesBlock.isEmpty() ? qcAirBlockId : resolve(definition.leavesBlock, qcAirBlockId, warnings);
        feature.minHeight = definition.minHeight;
        feature.maxHeight = definition.maxHeight;
        feature.cone = definition.shape == u"cone"_s;
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
        biome.baseHeight = definition.baseHeight;
        biome.heightVariation = definition.heightVariation;
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
            biome.features.append({int(it - m_content->features().begin()), placement.density});
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

QcTerrainGenerator::Column QcTerrainGenerator::column(int x, int z) const
{
    const double temperature =
        std::clamp(0.5 + 1.1 * m_temperatureNoise.fractal2(x / temperatureScale, z / temperatureScale, 3), 0.0, 1.0);
    const double humidity = std::clamp(0.5 + 1.1 * m_humidityNoise.fractal2(x / humidityScale, z / humidityScale, 3), 0.0, 1.0);
    const double detail = std::clamp(1.6 * m_detailNoise.fractal2(x / detailScale, z / detailScale, 5), -1.0, 1.0);

    // Inverse-distance weighting in climate space blends heights smoothly across biome borders.
    double weightSum = 0.0;
    double landHeight = 0.0;
    double bestWeight = -1.0;
    Column result;
    for (qsizetype i = 0; i < m_biomes.size(); ++i) {
        const Biome &biome = m_biomes[i];
        const double dt = temperature - biome.temperature;
        const double dh = humidity - biome.humidity;
        const double distanceSquared = dt * dt + dh * dh + 0.0015;
        const double weight = 1.0 / (distanceSquared * distanceSquared);
        weightSum += weight;
        landHeight += weight * (biome.baseHeight + biome.heightVariation * detail);
        if (weight > bestWeight) {
            bestWeight = weight;
            result.biome = int(i);
        }
    }
    landHeight /= weightSum;

    const double continent = 1.4 * m_continentNoise.fractal2(x / continentScale, z / continentScale, 4);
    result.landFactor = smoothstep(-0.30, -0.05, continent);
    const double oceanFloor = m_seaLevel - oceanDepth + 5.0 * detail;
    const double height = oceanFloor + (landHeight - oceanFloor) * result.landFactor;
    result.height = std::clamp(int(std::lround(height)), 4, QcWorld::worldHeight - 8);
    return result;
}

QcBlockId QcTerrainGenerator::surfaceBlock(Column column) const
{
    const Biome &biome = m_biomes[column.biome];
    const int top = column.height - 1;
    if (top < m_seaLevel - 1) {
        return biome.underwater;
    }
    if (top <= m_seaLevel + 1) {
        return biome.beach;
    }
    if (biome.snowLine > 0 && top >= biome.snowLine) {
        return biome.snow;
    }
    return biome.surface;
}

double QcTerrainGenerator::caveDensity(int field, int x, int y, int z) const
{
    switch (field) {
    case TunnelA:
        return m_tunnelNoiseA.noise3(x / tunnelHorizontalScale, y / tunnelVerticalScale, z / tunnelHorizontalScale);
    case TunnelB:
        return m_tunnelNoiseB.noise3(x / tunnelHorizontalScale, y / tunnelVerticalScale, z / tunnelHorizontalScale);
    default:
        return m_cavernNoise.fractal3(x / cavernHorizontalScale, y / cavernVerticalScale, z / cavernHorizontalScale, 2);
    }
}

bool QcTerrainGenerator::isCave(int x, int y, int z) const
{
    if (!m_caves || y <= 0) {
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
    const bool tunnel = std::abs(values[TunnelA]) < m_tunnelWidth && std::abs(values[TunnelB]) < m_tunnelWidth;
    const bool cavern = y < cavernMaxY && values[Cavern] > m_cavernThreshold;
    return tunnel || cavern;
}

void QcTerrainGenerator::fillColumn(QcChunk &chunk, int localX, int localZ, Column column) const
{
    const Biome &biome = m_biomes[column.biome];
    const int top = column.height - 1;
    const QcBlockId surface = surfaceBlock(column);
    const QcBlockId subsurface = top < m_seaLevel - 1 ? biome.underwater : (surface == biome.beach ? biome.beach : biome.subsurface);
    const int subsurfaceBottom = top - biome.subsurfaceDepth;
    const int worldX = chunk.pos().x * QcWorld::chunkSize + localX;
    const int worldZ = chunk.pos().z * QcWorld::chunkSize + localZ;

    for (int y = 0; y <= top; ++y) {
        QcBlockId block = m_stone;
        if (y == top) {
            block = surface;
        } else if (y >= subsurfaceBottom) {
            block = subsurface;
        }
        if (y == 0 || (y <= 2 && qcPositionRandom(m_seed, worldX, y, worldZ) < 0.5)) {
            block = m_bedrock;
        }
        chunk.setBlock(localX, y, localZ, block);
    }
    for (int y = column.height; y < m_seaLevel; ++y) {
        chunk.setBlock(localX, y, localZ, y == m_seaLevel - 1 ? biome.frozenWater : m_water);
    }
}

void QcTerrainGenerator::carveCaves(QcChunk &chunk, const QList<Column> &columns) const
{
    if (!m_caves) {
        return;
    }
    const int baseX = chunk.pos().x * QcWorld::chunkSize;
    const int baseZ = chunk.pos().z * QcWorld::chunkSize;
    int maxHeight = 0;
    for (const Column &column : columns) {
        maxHeight = std::max(maxHeight, column.height);
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
            const Column &column = columns[lz * QcWorld::chunkSize + lx];
            // Keep a solid lid under seas, lakes and beaches so water never pours into caves.
            const bool wet = column.height <= m_seaLevel + 1;
            const int top = wet ? column.height - 6 : column.height - 1;
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
                const bool tunnel = std::abs(values[TunnelA]) < m_tunnelWidth && std::abs(values[TunnelB]) < m_tunnelWidth;
                const bool cavern = y < cavernMaxY && values[Cavern] > m_cavernThreshold;
                if ((tunnel || cavern) && chunk.block(lx, y, lz) != m_bedrock) {
                    chunk.setBlock(lx, y, lz, qcAirBlockId);
                }
            }
        }
    }
}

void QcTerrainGenerator::placeOres(QcChunk &chunk) const
{
    const QcChunkPos pos = chunk.pos();
    for (const Ore &ore : m_ores) {
        for (int vein = 0; vein < ore.veins; ++vein) {
            const auto random = [&](int salt) { return qcPositionRandom(ore.seed, pos.x, vein * 64 + salt, pos.z); };
            int x = int(random(0) * QcWorld::chunkSize);
            int y = ore.minY + int(random(1) * (ore.maxY - ore.minY + 1));
            int z = int(random(2) * QcWorld::chunkSize);
            for (int step = 0; step < ore.veinSize; ++step) {
                if (y >= 0 && y < QcWorld::worldHeight && chunk.block(x, y, z) == ore.replaces) {
                    chunk.setBlock(x, y, z, ore.block);
                }
                // Random walk, kept inside this chunk so veins never depend on neighbours.
                x = std::clamp(x + int(random(3 + step * 3) * 3.0) - 1, 0, QcWorld::chunkSize - 1);
                y = std::clamp(y + int(random(4 + step * 3) * 3.0) - 1, ore.minY, ore.maxY);
                z = std::clamp(z + int(random(5 + step * 3) * 3.0) - 1, 0, QcWorld::chunkSize - 1);
            }
        }
    }
}

void QcTerrainGenerator::placeFeature(QcChunk &chunk, const Feature &feature, int x, int baseY, int z, double random) const
{
    const int baseX = chunk.pos().x * QcWorld::chunkSize;
    const int baseZ = chunk.pos().z * QcWorld::chunkSize;
    const auto put = [&](int wx, int wy, int wz, QcBlockId block, bool onlyIntoAir) {
        const int lx = wx - baseX;
        const int lz = wz - baseZ;
        if (lx < 0 || lx >= QcWorld::chunkSize || lz < 0 || lz >= QcWorld::chunkSize || wy < 0 || wy >= QcWorld::worldHeight) {
            return;
        }
        if (onlyIntoAir && chunk.block(lx, wy, lz) != qcAirBlockId) {
            return;
        }
        chunk.setBlock(lx, wy, lz, block);
    };
    const int height = feature.minHeight + int(random * (feature.maxHeight - feature.minHeight + 1));
    for (int dy = 0; dy < height; ++dy) {
        put(x, baseY + dy, z, feature.trunk, false);
    }
    if (feature.type != QcFeatureDefinition::Type::Tree) {
        return;
    }
    if (feature.cone) {
        // Spruce-like: rings shrinking towards the top, starting two blocks above the ground.
        const int crownBottom = baseY + 2;
        const int crownTop = baseY + height;
        for (int y = crownBottom; y <= crownTop; ++y) {
            const int radius = std::max(0, std::min(3, (crownTop - y + 2) / 2));
            for (int dx = -radius; dx <= radius; ++dx) {
                for (int dz = -radius; dz <= radius; ++dz) {
                    if (std::abs(dx) + std::abs(dz) <= radius + 1) {
                        put(x + dx, y, z + dz, feature.leaves, true);
                    }
                }
            }
        }
        return;
    }
    // Round crown: two wide layers below the top, two narrow layers at the top.
    const int top = baseY + height;
    for (int y = top - 3; y <= top; ++y) {
        const int radius = y >= top - 1 ? 1 : 2;
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dz = -radius; dz <= radius; ++dz) {
                const bool corner = std::abs(dx) == radius && std::abs(dz) == radius;
                if (corner && (y == top || qcPositionRandom(m_seed, x + dx, y, z + dz) < 0.5)) {
                    continue;
                }
                put(x + dx, y, z + dz, feature.leaves, true);
            }
        }
    }
}

void QcTerrainGenerator::placeFeatures(QcChunk &chunk) const
{
    const int baseX = chunk.pos().x * QcWorld::chunkSize;
    const int baseZ = chunk.pos().z * QcWorld::chunkSize;
    for (int z = baseZ - featureMargin; z < baseZ + QcWorld::chunkSize + featureMargin; ++z) {
        for (int x = baseX - featureMargin; x < baseX + QcWorld::chunkSize + featureMargin; ++x) {
            const Column info = column(x, z);
            const Biome &biome = m_biomes[info.biome];
            if (biome.features.isEmpty() || info.height - 1 <= m_seaLevel) {
                continue;
            }
            for (const auto &[featureIndex, density] : biome.features) {
                const quint64 featureSeed = qcMixSeed(m_seed, 1000 + quint64(featureIndex));
                if (qcPositionRandom(featureSeed, x, 0, z) >= density) {
                    continue;
                }
                const Feature &feature = m_features[featureIndex];
                const QcBlockId ground = surfaceBlock(info);
                // Skip columns whose ground was carved away by a cave opening.
                if ((!feature.ground.isEmpty() && !feature.ground.contains(ground)) || isCave(x, info.height - 1, z)) {
                    break;
                }
                placeFeature(chunk, feature, x, info.height, z, qcPositionRandom(featureSeed, x, 1, z));
                break;
            }
        }
    }
}

std::shared_ptr<QcChunk> QcTerrainGenerator::generate(QcChunkPos pos) const
{
    auto chunk = std::make_shared<QcChunk>(pos);
    QList<Column> columns;
    columns.reserve(qsizetype(QcWorld::chunkSize) * QcWorld::chunkSize);
    for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
        for (int lx = 0; lx < QcWorld::chunkSize; ++lx) {
            columns.append(column(pos.x * QcWorld::chunkSize + lx, pos.z * QcWorld::chunkSize + lz));
        }
    }
    for (int lz = 0; lz < QcWorld::chunkSize; ++lz) {
        for (int lx = 0; lx < QcWorld::chunkSize; ++lx) {
            fillColumn(*chunk, lx, lz, columns[lz * QcWorld::chunkSize + lx]);
        }
    }
    carveCaves(*chunk, columns);
    placeOres(*chunk);
    placeFeatures(*chunk);
    for (int i = 0; i < QcWorld::sectionCount; ++i) {
        chunk->section(i).compact();
    }
    return chunk;
}

QcBlockPos QcTerrainGenerator::spawnPoint() const
{
    // Nearest dry, not too high land on a spiral of growing squares around the origin.
    for (int radius = 0; radius <= spawnSearchRadius; radius += 8) {
        for (int dx = -radius; dx <= radius; dx += 8) {
            for (int dz = -radius; dz <= radius; dz += 8) {
                if (std::max(std::abs(dx), std::abs(dz)) != radius) {
                    continue;
                }
                const Column info = column(dx, dz);
                if (info.landFactor > 0.99 && info.height > m_seaLevel + 2 && info.height < m_seaLevel + 30
                    && !isCave(dx, info.height - 1, dz)) {
                    return {dx, info.height, dz};
                }
            }
        }
    }
    return {0, std::max(column(0, 0).height, m_seaLevel), 0};
}
