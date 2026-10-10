#include "QcTerrainDefinitions.h"

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

namespace {

using Biome = QcTerrainDefinitions::Biome;
using Feature = QcTerrainDefinitions::Feature;
using Ore = QcTerrainDefinitions::Ore;
using Pool = QcTerrainDefinitions::Pool;

// Resolves block names of one registry and collects the warnings about them.
class Loader
{
public:
    Loader(const QcContentRegistry &content, QStringList *warnings)
        : m_content(content)
        , m_warnings(warnings)
    {
    }

    QcBlockId resolve(const QString &name, QcBlockId fallback) const;
    std::optional<Pool> pool(const QcFeatureDefinition &definition, quint64 seed) const;
    Feature feature(const QcFeatureDefinition &definition, quint64 patchSeed) const;
    // `stone` and `water` are the fallbacks of the biome's own blocks.
    Biome biome(const QcBiomeDefinition &definition, QcBlockId stone, QcBlockId water) const;
    void warnSharedClimate(const QList<Biome> &biomes, const Biome &biome) const;
    std::optional<Ore> ore(const QcOreDefinition &definition, QcBlockId stone, quint64 seed) const;

private:
    void addFeatures(Biome &biome, const QcBiomeDefinition &definition) const;
    void warn(const QString &warning) const
    {
        if (m_warnings) {
            m_warnings->append(warning);
        }
    }

    const QcContentRegistry &m_content;
    QStringList *m_warnings = nullptr;
};

QcBlockId Loader::resolve(const QString &name, QcBlockId fallback) const
{
    // An omitted optional block simply uses the fallback.
    if (name.isEmpty()) {
        return fallback;
    }
    const std::optional<QcBlockId> id = m_content.blockId(name);
    if (!id) {
        warn(u"World generation references unknown block '%1'"_s.arg(name));
        return fallback;
    }
    return *id;
}

std::optional<Pool> Loader::pool(const QcFeatureDefinition &definition, quint64 seed) const
{
    Pool pool;
    pool.liquid = resolve(definition.trunkBlock, qcAirBlockId);
    pool.minY = std::max(definition.minY, definition.depth + 1);
    pool.maxY = definition.maxY;
    pool.attempts = definition.attempts;
    pool.chance = definition.chance;
    pool.radius = definition.radius;
    pool.depth = definition.depth;
    pool.seed = seed;
    pool.surface = definition.surface;

    for (const QString &biome : definition.biomes) {
        const auto found = std::ranges::find(m_content.biomes(), biome, &QcBiomeDefinition::name);
        if (found != m_content.biomes().end()) {
            pool.biomes.insert(int(found - m_content.biomes().begin()));
        } else {
            warn(u"Pool %1 references unknown biome %2"_s.arg(definition.name, biome));
        }
    }
    if (pool.liquid == qcAirBlockId || pool.minY > pool.maxY) {
        return std::nullopt;
    }
    return pool;
}

Feature Loader::feature(const QcFeatureDefinition &definition, quint64 patchSeed) const
{
    Feature feature;
    feature.shape.type = definition.type;
    feature.shape.trunk = resolve(definition.trunkBlock, qcAirBlockId);
    feature.shape.leaves = definition.leavesBlock.isEmpty() ? qcAirBlockId : resolve(definition.leavesBlock, qcAirBlockId);
    feature.shape.minHeight = definition.minHeight;
    feature.shape.maxHeight = definition.maxHeight;
    feature.shape.cone = definition.shape == u"cone"_s;
    feature.nearWater = definition.nearWater;
    feature.patchSize = definition.patchSize;
    feature.patches = QcNoise(patchSeed);
    for (const QString &ground : definition.groundBlocks) {
        feature.ground.insert(resolve(ground, qcAirBlockId));
    }
    return feature;
}

Biome Loader::biome(const QcBiomeDefinition &definition, QcBlockId stone, QcBlockId water) const
{
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
    biome.steep = resolve(definition.steepBlock, qcAirBlockId);
    biome.steepSlope = definition.steepSlope;

    biome.surface = resolve(definition.surfaceBlock, stone);
    biome.subsurface = resolve(definition.subsurfaceBlock, biome.surface);
    biome.subsurfaceDepth = definition.subsurfaceDepth;
    biome.underwater = resolve(definition.underwaterBlock, biome.subsurface);
    biome.beach = resolve(definition.beachBlock, biome.surface);
    biome.frozenWater = definition.frozenWaterBlock.isEmpty() ? water : resolve(definition.frozenWaterBlock, water);
    biome.snowLine = definition.snowLine;
    biome.snow = definition.snowBlock.isEmpty() ? biome.surface : resolve(definition.snowBlock, biome.surface);

    addFeatures(biome, definition);
    return biome;
}

void Loader::addFeatures(Biome &biome, const QcBiomeDefinition &definition) const
{
    for (const QcFeaturePlacement &placement : definition.features) {
        const auto it = std::ranges::find(m_content.features(), placement.feature, &QcFeatureDefinition::name);
        if (it == m_content.features().end()) {
            warn(u"Biome %1 references unknown feature %2"_s.arg(definition.name, placement.feature));
            continue;
        }
        if (it->type == QcFeatureDefinition::Type::Pool) {
            warn(u"Biome %1 lists the pool %2; pools are placed in every chunk"_s.arg(definition.name, placement.feature));
            continue;
        }
        biome.features.append({int(it - m_content.features().begin()), placement.density});
    }
}

void Loader::warnSharedClimate(const QList<Biome> &biomes, const Biome &biome) const
{
    for (const Biome &other : biomes) {
        if (other.temperature == biome.temperature && other.humidity == biome.humidity && other.ruggedness == biome.ruggedness) {
            warn(u"Biomes %1 and %2 share a climate point; only %1 can appear"_s.arg(other.name, biome.name));
        }
    }
}

std::optional<Ore> Loader::ore(const QcOreDefinition &definition, QcBlockId stone, quint64 seed) const
{
    Ore ore;
    ore.block = resolve(definition.block, qcAirBlockId);
    ore.replaces = resolve(definition.replaces, stone);
    ore.minY = definition.minY;
    ore.maxY = definition.maxY;
    ore.veins = definition.veinsPerChunk;
    ore.veinSize = definition.veinSize;
    ore.seed = seed;
    if (ore.block == qcAirBlockId) {
        return std::nullopt;
    }
    return ore;
}

QSet<QcBlockId> naturalGroundOf(const QList<QcTerrainDefinitions::Biome> &biomes, QcBlockId stone, QcBlockId water)
{
    QSet<QcBlockId> ground{stone};
    for (const QcTerrainDefinitions::Biome &biome : biomes) {
        for (const QcBlockId block : {biome.surface, biome.subsurface, biome.underwater, biome.beach, biome.steep, biome.snow}) {
            ground.insert(block);
        }
    }

    ground.remove(qcAirBlockId);
    ground.remove(water);
    return ground;
}

} // namespace

QcTerrainDefinitions::QcTerrainDefinitions(const QcContentRegistry &content, quint64 seed, QStringList *warnings)
{
    const Loader loader(content, warnings);
    const QcTerrainSettings &terrain = content.terrainSettings();
    seaLevel = terrain.seaLevel;
    stone = loader.resolve(terrain.stoneBlock, qcAirBlockId);
    water = loader.resolve(terrain.waterBlock, qcAirBlockId);
    bedrock = terrain.bedrockBlock.isEmpty() ? stone : loader.resolve(terrain.bedrockBlock, stone);
    caveLiquid = terrain.caveLiquidBlock.isEmpty() ? qcAirBlockId : loader.resolve(terrain.caveLiquidBlock, qcAirBlockId);
    bedrockLayers = terrain.bedrockLayers;

    // Seeds come from the names (decision E-214): new content leaves the other features and ores
    // where they were.
    for (const QcFeatureDefinition &definition : content.features()) {
        const quint64 salt = qcNameSalt(definition.name);
        if (definition.type == QcFeatureDefinition::Type::Pool) {
            if (const std::optional<Pool> pool = loader.pool(definition, qcMixSeed(seed, salt + 3))) {
                pools.append(*pool);
            }
        }
        Feature feature = loader.feature(definition, qcMixSeed(seed, salt + 2));
        feature.seed = qcMixSeed(seed, salt + 1);
        features.append(feature);
    }

    for (const QcBiomeDefinition &definition : content.biomes()) {
        const Biome biome = loader.biome(definition, stone, water);
        anyOverhangs = anyOverhangs || biome.overhang > 0;
        anyRidges = anyRidges || biome.ridges > 0.0;
        anyTerraces = anyTerraces || biome.terraceHeight > 0;
        loader.warnSharedClimate(biomes, biome);
        biomes.append(biome);
    }
    if (biomes.isEmpty()) {
        // Without any biome data the world is bare stone hills, which keeps the game playable.
        Biome fallback;
        fallback.name = u"qtcraft:fallback"_s;
        fallback.surface = fallback.subsurface = fallback.underwater = fallback.beach = stone;
        fallback.frozenWater = fallback.snow = stone;
        biomes.append(fallback);
        if (warnings) {
            warnings->append(u"No biomes defined; using a stone fallback"_s);
        }
    }

    naturalGround = naturalGroundOf(biomes, stone, water);

    for (const QcOreDefinition &definition : content.ores()) {
        if (const std::optional<Ore> ore = loader.ore(definition, stone, qcMixSeed(seed, qcNameSalt(definition.name) + 4))) {
            ores.append(*ore);
        }
    }
}
