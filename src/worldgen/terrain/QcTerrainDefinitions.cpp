#include "worldgen/terrain/QcTerrainDefinitions.h"

#include <algorithm>
#include <optional>

using namespace Qt::StringLiterals;

namespace {

using Biome = QcTerrainDefinitions::Biome;
using Feature = QcTerrainDefinitions::Feature;
using Ore = QcTerrainDefinitions::Ore;
using Pool = QcTerrainDefinitions::Pool;

// The resolved vegetation of one biome (decision E-814), applied by the constructor.
struct BiomeVegetation
{
    std::array<QcTerrainDefinitions::VegetationPatches, QcTerrainDefinitions::VegetationLayers> patches;
    // The features of the scattered plants with their densities per column.
    QList<std::pair<Feature, double>> scattered;
};

// Resolves block names of one registry and collects the warnings about them.
class Loader
{
public:
    explicit Loader(const QcContentRegistry &content)
        : m_content(content)
    {
    }

    QcBlockId resolve(const QString &name, QcBlockId fallback);
    std::optional<Pool> pool(const QcFeatureDefinition &definition, quint64 seed);
    Feature feature(const QcFeatureDefinition &definition, quint64 patchSeed);
    // `stone` and `water` are the fallbacks of the biome's own blocks.
    Biome biome(const QcBiomeDefinition &definition, QcBlockId stone, QcBlockId water);
    void warnSharedClimate(const QList<Biome> &biomes, const Biome &biome);
    // Warns about the fields a river biome sets that do not apply to it (decision E-840).
    void warnRiverFields(const QcBiomeDefinition &definition)
    {
        if (definition.river && !definition.riverBiome.isEmpty()) {
            warn(u"River biome %1 names a river biome of its own, which is ignored"_s.arg(definition.name));
        }
        if (definition.river && definition.rarity > 0.0) {
            warn(u"River biome %1 has a rarity, which is ignored"_s.arg(definition.name));
        }
    }
    // The river biome of every biome (decision E-840), -1 for river biomes and without one: the one
    // a land biome names, else the terrain's `riverBiome` (`fallback`), else the first.
    [[nodiscard]] QList<int> riverBiomes(const QList<Biome> &biomes, const QString &fallback);
    // The vegetation layers of a biome (decision E-814): a feature for the scattered plants of each
    // layer with a density, placed after the biome's own features, and the patches.
    [[nodiscard]] BiomeVegetation vegetation(const QcBiomeDefinition &definition, quint64 seed);
    std::optional<Ore> ore(const QcOreDefinition &definition, QcBlockId stone, quint64 seed);
    void warn(const QString &warning) { m_warnings.append(warning); }
    // Warns when the terrain's `vegetationBiome` names no biome.
    void warnUnknownBiome(const QList<Biome> &biomes, const QString &vegetationBiome)
    {
        if (!vegetationBiome.isEmpty() && std::ranges::none_of(biomes, [&vegetationBiome](const Biome &biome) { return biome.name == vegetationBiome; })) {
            warn(u"Terrain names the unknown vegetation biome %1"_s.arg(vegetationBiome));
        }
    }
    [[nodiscard]] QStringList takeWarnings() { return std::exchange(m_warnings, {}); }

private:
    // The features a biome places (pools and unknown ones left out with a warning).
    [[nodiscard]] decltype(Biome::features) featuresOf(const QcBiomeDefinition &definition);
    // Whether the feature at this index is a giant tree (decision E-822).
    [[nodiscard]] bool isGiant(int feature) const;

    const QcContentRegistry &m_content;
    QStringList m_warnings;
};

QcBlockId Loader::resolve(const QString &name, QcBlockId fallback)
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

std::optional<Pool> Loader::pool(const QcFeatureDefinition &definition, quint64 seed)
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
        const auto found = std::ranges::find(m_content.worldgen().biomes(), biome, &QcBiomeDefinition::name);
        if (found != m_content.worldgen().biomes().end()) {
            pool.biomes.insert(int(found - m_content.worldgen().biomes().begin()));
        } else {
            warn(u"Pool %1 references unknown biome %2"_s.arg(definition.name, biome));
        }
    }
    if (pool.liquid == qcAirBlockId || pool.minY > pool.maxY) {
        return std::nullopt;
    }
    return pool;
}

Feature Loader::feature(const QcFeatureDefinition &definition, quint64 patchSeed)
{
    Feature feature;
    feature.shape.type = definition.type;
    feature.shape.trunk = resolve(definition.trunkBlock, qcAirBlockId);
    feature.shape.leaves = definition.leavesBlock.isEmpty() ? qcAirBlockId : resolve(definition.leavesBlock, qcAirBlockId);
    feature.shape.minHeight = definition.minHeight;
    feature.shape.maxHeight = definition.maxHeight;
    feature.shape.crown = QcTreeShape::crownOf(definition.shape).value_or(QcTreeShape::Crown::Round);
    std::tie(feature.shape.trunkX, feature.shape.trunkZ) = QcTreeShape::lyingTrunks(m_content, feature.shape.trunk);
    if (!definition.vinesBlock.isEmpty()) {
        feature.shape.vines = QcTreeShape::vinesOf(m_content, resolve(definition.vinesBlock, qcAirBlockId));
    }
    feature.nearWater = definition.nearWater;
    feature.onWater = definition.onWater;
    feature.patchSize = definition.patchSize;
    feature.patches = QcNoise(patchSeed);
    for (const QString &ground : definition.groundBlocks) {
        feature.ground.insert(resolve(ground, qcAirBlockId));
    }
    return feature;
}

Biome Loader::biome(const QcBiomeDefinition &definition, QcBlockId stone, QcBlockId water)
{
    Biome biome;
    biome.name = definition.name;
    biome.temperature = definition.temperature;
    biome.humidity = definition.humidity;
    biome.ruggedness = definition.ruggedness;
    biome.rarity = definition.rarity;
    biome.spread = definition.spread;
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
    for (const QcBlockShare &patch : definition.underwaterPatches) {
        // A river bed is ground: patches of air, plants or liquids would leave holes or open water.
        const QcBlockId block = resolve(patch.block, biome.underwater);
        const QcBlockDefinition &patchBlock = m_content.block(block);
        if (!patchBlock.solid || patchBlock.liquid) {
            warn(u"Biome %1 has the underwater patch %2, which is no solid block; left out"_s.arg(definition.name, patch.block));
            continue;
        }
        biome.underwaterPatches.append({block, patch.share});
    }
    biome.beach = resolve(definition.beachBlock, biome.surface);
    biome.frozenWater = definition.frozenWaterBlock.isEmpty() ? water : resolve(definition.frozenWaterBlock, water);
    biome.snowLine = definition.snowLine;
    biome.snow = definition.snowBlock.isEmpty() ? biome.surface : resolve(definition.snowBlock, biome.surface);
    biome.river = definition.river;

    // Giant trees are placed per cell (decision E-822): their density per column becomes the chance
    // of one in a whole cell.
    const decltype(Biome::features) placed = featuresOf(definition);
    for (const auto &[feature, density] : placed) {
        if (isGiant(feature)) {
            constexpr double cellArea = double(QcTerrainDefinitions::giantCell) * QcTerrainDefinitions::giantCell;
            biome.giants.append({feature, std::min(1.0, density * cellArea)});
        } else {
            biome.features.append({feature, density});
        }
    }
    return biome;
}

decltype(Biome::features) Loader::featuresOf(const QcBiomeDefinition &definition)
{
    decltype(Biome::features) features;
    for (const QcFeaturePlacement &placement : definition.features) {
        const auto it = std::ranges::find(m_content.worldgen().features(), placement.feature, &QcFeatureDefinition::name);
        if (it == m_content.worldgen().features().end()) {
            warn(u"Biome %1 references unknown feature %2"_s.arg(definition.name, placement.feature));
            continue;
        }
        if (it->type == QcFeatureDefinition::Type::Pool) {
            warn(u"Biome %1 lists the pool %2; pools are placed in every chunk"_s.arg(definition.name, placement.feature));
            continue;
        }
        features.append({int(it - m_content.worldgen().features().begin()), placement.density});
    }
    return features;
}

bool Loader::isGiant(int feature) const
{
    const QcFeatureDefinition &definition = m_content.worldgen().features()[feature];
    return definition.type == QcFeatureDefinition::Type::Tree && QcTreeShape::paramsOf(m_content, definition).giant();
}

void Loader::warnSharedClimate(const QList<Biome> &biomes, const Biome &biome)
{
    if (biome.river) {
        return;
    }
    for (const Biome &other : biomes) {
        if (!other.river && other.temperature == biome.temperature && other.humidity == biome.humidity && other.ruggedness == biome.ruggedness
            && other.rarity == biome.rarity) {
            warn(u"Biomes %1 and %2 share a climate point; only %1 can appear"_s.arg(other.name, biome.name));
        }
    }
}

QList<int> Loader::riverBiomes(const QList<Biome> &biomes, const QString &fallback)
{
    const auto first = std::ranges::find_if(biomes, &Biome::river);
    int firstRiver = first == biomes.end() ? -1 : int(first - biomes.begin());
    if (!fallback.isEmpty()) {
        const auto named = std::ranges::find(biomes, fallback, &Biome::name);
        if (named != biomes.end() && named->river) {
            firstRiver = int(named - biomes.begin());
        } else {
            warn(u"Terrain names %1 as the river biome, which is no river biome"_s.arg(fallback));
        }
    }
    if (!biomes.isEmpty() && std::ranges::all_of(biomes, &Biome::river)) {
        // The climate then chooses none: every column takes the first biome.
        warn(u"Every biome is a river biome; the climate chooses none"_s);
    }

    QList<int> rivers(biomes.size(), -1);
    const QList<QcBiomeDefinition> &definitions = m_content.worldgen().biomes();
    for (qsizetype i = 0; i < biomes.size() && i < definitions.size(); ++i) {
        const QString &named = definitions[i].riverBiome;
        if (biomes[i].river || named.isEmpty()) {
            rivers[i] = biomes[i].river ? -1 : firstRiver;
            continue;
        }
        const auto found = std::ranges::find(biomes, named, &Biome::name);
        if (found == biomes.end() || !found->river) {
            warn(u"Biome %1 names %2 as its river biome, which is no river biome"_s.arg(biomes[i].name, named));
        }
        rivers[i] = found == biomes.end() || !found->river ? firstRiver : int(found - biomes.begin());
    }
    return rivers;
}

// The biome of a terrain without biome data: bare stone hills, which keep the game playable.
Biome fallbackBiome(QcBlockId stone)
{
    Biome fallback;
    fallback.name = u"qtcraft:fallback"_s;
    fallback.surface = fallback.subsurface = fallback.underwater = fallback.beach = stone;
    fallback.frozenWater = fallback.snow = stone;
    return fallback;
}

std::optional<Ore> Loader::ore(const QcOreDefinition &definition, QcBlockId stone, quint64 seed)
{
    Ore ore;
    ore.block = resolve(definition.block, qcAirBlockId);
    ore.replaces = resolve(definition.replaces, stone);
    ore.minY = definition.minY;
    ore.maxY = definition.maxY;
    ore.veins = definition.veinsPerChunk;
    ore.veinSize = definition.veinSize;
    ore.seed = seed;
    for (const QString &biome : definition.biomes) {
        const auto found = std::ranges::find(m_content.worldgen().biomes(), biome, &QcBiomeDefinition::name);
        if (found != m_content.worldgen().biomes().end()) {
            ore.biomes.insert(int(found - m_content.worldgen().biomes().begin()));
        } else {
            warn(u"Ore %1 references unknown biome %2"_s.arg(definition.name, biome));
        }
    }
    if (ore.block == qcAirBlockId) {
        return std::nullopt;
    }
    return ore;
}

BiomeVegetation Loader::vegetation(const QcBiomeDefinition &definition, quint64 seed)
{
    BiomeVegetation vegetation;
    const std::array<std::pair<const QcVegetationLayer *, QString>, QcTerrainDefinitions::VegetationLayers> layers{
        {{&definition.vegetation.grass, u"#grass"_s}, {&definition.vegetation.flowers, u"#flowers"_s}}};
    for (std::size_t index = 0; index < layers.size(); ++index) {
        const auto &[layer, suffix] = layers[index];
        QcVegetationTable::Resolved resolved = QcVegetationTable::resolve(m_content, *layer);
        for (const QString &unknown : std::as_const(resolved.unknown)) {
            warn(u"Biome %1 lists the unknown plant %2"_s.arg(definition.name, unknown));
        }
        for (const QString &notPlant : std::as_const(resolved.notPlants)) {
            warn(u"Biome %1 lists %2 among its plants, which is solid or a liquid"_s.arg(definition.name, notPlant));
        }
        if (resolved.table.isEmpty()) {
            continue;
        }

        vegetation.patches[index] = {resolved.table, layer->patches};
        if (layer->density > 0.0) {
            const quint64 salt = qcNameSalt(definition.name + suffix);
            Feature feature;
            feature.shape.type = QcFeatureDefinition::Type::Plant;
            feature.patchSize = layer->patchSize;
            feature.patches = QcNoise(qcMixSeed(seed, salt + 2));
            feature.seed = qcMixSeed(seed, salt + 1);
            feature.plants = std::move(resolved.table);
            vegetation.scattered.append({std::move(feature), layer->density});
        }
    }
    return vegetation;
}

QSet<QcBlockId> naturalGroundOf(const QList<QcTerrainDefinitions::Biome> &biomes, QcBlockId stone, QcBlockId water)
{
    QSet<QcBlockId> ground{stone};
    for (const QcTerrainDefinitions::Biome &biome : biomes) {
        for (const QcBlockId block : {biome.surface, biome.subsurface, biome.underwater, biome.beach, biome.steep, biome.snow}) {
            ground.insert(block);
        }
        for (const auto &[block, share] : biome.underwaterPatches) {
            ground.insert(block);
        }
    }

    ground.remove(qcAirBlockId);
    ground.remove(water);
    return ground;
}

// The chance of any giant tree per cell of the biome.
double giantChanceOf(const QcTerrainDefinitions::Biome &biome)
{
    double sum = 0.0;
    for (const auto &[feature, chance] : biome.giants) {
        sum += chance;
    }
    return sum;
}

} // namespace

QcTerrainDefinitions::QcTerrainDefinitions(const QcContentRegistry &content, quint64 seed)
{
    Loader loader(content);
    const QcTerrainSettings &terrain = content.worldgen().terrainSettings();
    seaLevel = terrain.seaLevel;
    stone = loader.resolve(terrain.stoneBlock, qcAirBlockId);
    water = loader.resolve(terrain.waterBlock, qcAirBlockId);
    bedrock = terrain.bedrockBlock.isEmpty() ? stone : loader.resolve(terrain.bedrockBlock, stone);
    caveLiquid = terrain.caveLiquidBlock.isEmpty() ? qcAirBlockId : loader.resolve(terrain.caveLiquidBlock, qcAirBlockId);
    bedrockLayers = terrain.bedrockLayers;
    rivers = terrain.rivers;

    // Seeds come from the names (decision E-214): new content leaves the other features and ores
    // where they were.
    for (const QcFeatureDefinition &definition : content.worldgen().features()) {
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

    for (const QcBiomeDefinition &definition : content.worldgen().biomes()) {
        Biome biome = loader.biome(definition, stone, water);
        BiomeVegetation vegetation = loader.vegetation(definition, seed);
        for (auto &[feature, density] : vegetation.scattered) {
            biome.features.append({int(features.size()), density});
            features.append(std::move(feature));
        }
        for (std::size_t layer = 0; layer < vegetation.patches.size(); ++layer) {
            maxPatchChance[layer] = std::max(maxPatchChance[layer], vegetation.patches[layer].patches.chance);
        }
        biome.patches = std::move(vegetation.patches);
        maxGiantChance = std::max(maxGiantChance, giantChanceOf(biome));
        anyOverhangs = anyOverhangs || biome.overhang > 0;
        anyRidges = anyRidges || biome.ridges > 0.0;
        anyTerraces = anyTerraces || biome.terraceHeight > 0;
        // River biomes take no part in the climate's choice, their rarity included.
        anyRare = anyRare || (!biome.river && biome.rarity > 0.0);
        loader.warnRiverFields(definition);
        loader.warnSharedClimate(biomes, biome);
        biomes.append(biome);
    }
    if (biomes.isEmpty()) {
        biomes.append(fallbackBiome(stone));
        loader.warn(u"No biomes defined; using a stone fallback"_s);
    }
    const QList<int> riverOf = loader.riverBiomes(biomes, terrain.riverBiome);
    for (qsizetype i = 0; i < biomes.size(); ++i) {
        biomes[i].riverBiome = riverOf[i];
    }
    if (std::ranges::none_of(biomes, [](const Biome &biome) { return !biome.river && biome.rarity <= 0.0; })) {
        // Outside the rarity spots no biome would have any weight; the terrain falls back to the
        // nearest biome (decision E-815).
        loader.warn(u"Every biome has a rarity; outside the rare spots the nearest biome is used"_s);
    }

    naturalGround = naturalGroundOf(biomes, stone, water);
    loader.warnUnknownBiome(biomes, terrain.vegetationBiome);

    for (const QcOreDefinition &definition : content.worldgen().ores()) {
        if (const std::optional<Ore> ore = loader.ore(definition, stone, qcMixSeed(seed, qcNameSalt(definition.name) + 4))) {
            ores.append(*ore);
        }
    }
    warnings = loader.takeWarnings();
}
