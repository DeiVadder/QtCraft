#include "common/content/parser/QcWorldgenParser.h"

#include "common/content/parser/QcCoastalTowerParser.h"
#include "common/content/parser/QcContentFields.h"
#include "common/content/parser/QcJsonFieldReader.h"
#include "common/content/parser/QcVegetationParser.h"
#include "common/content/registry/QcContentNames.h"
#include "common/core/QcWorldConstants.h"

#include <QJsonArray>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

constexpr int maxPoolAttempts = 8;
// Largest square of chunks a surface lake is planned in (decision E-856).
constexpr int maxLakeRegion = 64;

// Reads one feature: owns the definition being built and the reader of its JSON object.
class FeatureReader
{
public:
    FeatureReader(const QJsonObject &json, const QString &packNamespace)
        : m_json(json)
        , m_namespace(packNamespace)
        , m_reader{json, u"feature"_s, {}}
    {
    }

    [[nodiscard]] QcResult<QcFeatureDefinition> read();

private:
    void readPool();

    const QJsonObject &m_json;
    const QString &m_namespace;
    QcJsonFieldReader m_reader;
    QcFeatureDefinition m_feature;
};

// The heights, tries, size and biomes of a pool.
void FeatureReader::readPool()
{
    m_feature.minY = m_reader.integer(u"minY"_s, m_feature.minY, 1, QcWorld::worldHeight - 2);
    m_feature.maxY = m_reader.integer(u"maxY"_s, std::max(m_feature.minY, m_feature.maxY), m_feature.minY, QcWorld::worldHeight - 2);
    m_feature.attempts = m_reader.integer(u"attempts"_s, m_feature.attempts, 1, maxPoolAttempts);
    m_feature.chance = m_reader.number(u"chance"_s, m_feature.chance, 0.0, 1.0);
    m_feature.radius = m_reader.integer(u"radius"_s, m_feature.radius, 1, 6);
    m_feature.depth = m_reader.integer(u"depth"_s, m_feature.depth, 1, 4);
    m_feature.surface = m_reader.boolean(u"surface"_s, false);
    if (m_feature.surface) {
        m_feature.region = m_reader.integer(u"region"_s, m_feature.region, 1, maxLakeRegion);
        m_feature.spacing = m_reader.integer(u"spacing"_s, m_feature.spacing, 0, m_feature.region - 1);
        m_feature.rimBlock = m_reader.name(u"rim"_s, m_namespace);
    }

    const QJsonValue biomes = m_json.value(u"biomes"_s);
    if (!biomes.isUndefined() && (!biomes.isArray() || biomes.toArray().size() > 64)) {
        m_reader.fail(u"'biomes' must be an array of at most 64 biome names"_s);
    }
    const QJsonArray biomeList = biomes.toArray();
    for (const auto &entry : biomeList) {
        const QString name = entry.isString() ? QcContentNames::qualify(entry.toString(), m_namespace) : QString();
        if (name.isEmpty()) {
            m_reader.fail(u"invalid 'biomes' entry"_s);
            break;
        }
        m_feature.biomes.append(name);
    }
}

QcResult<QcFeatureDefinition> FeatureReader::read()
{
    m_feature.name = m_reader.name(u"name"_s, m_namespace);
    if (m_feature.name.isEmpty()) {
        return qcError(m_reader.ok() ? u"feature: 'name' is required"_s : m_reader.error);
    }

    m_reader.context = u"feature '%1'"_s.arg(m_feature.name);
    const QString type = m_reader.string(u"type"_s, u"tree"_s);
    if (type == u"tree"_s) {
        m_feature.type = QcFeatureDefinition::Type::Tree;
    } else if (type == u"column"_s) {
        m_feature.type = QcFeatureDefinition::Type::Column;
    } else if (type == u"plant"_s) {
        m_feature.type = QcFeatureDefinition::Type::Plant;
    } else if (type == u"pool"_s) {
        m_feature.type = QcFeatureDefinition::Type::Pool;
    } else {
        m_reader.fail(u"'type' must be tree, column, plant or pool"_s);
    }

    const bool singleBlock = m_feature.type == QcFeatureDefinition::Type::Plant || m_feature.type == QcFeatureDefinition::Type::Pool;
    m_feature.trunkBlock = m_reader.name(singleBlock ? u"block"_s : u"trunk"_s, m_namespace);
    if (m_feature.type == QcFeatureDefinition::Type::Pool) {
        readPool();
    }

    m_feature.nearWater = m_reader.boolean(u"nearWater"_s, false);
    m_feature.onWater = m_reader.boolean(u"onWater"_s, false);
    if (m_reader.ok() && m_feature.onWater && (m_feature.nearWater || m_feature.type != QcFeatureDefinition::Type::Plant)) {
        m_reader.fail(u"'onWater' is for plants that are not 'nearWater'"_s);
    }
    m_feature.patchSize = m_reader.integer(u"patchSize"_s, 0, 0, 512);
    m_feature.leavesBlock = m_reader.name(u"leaves"_s, m_namespace);
    m_feature.minHeight = m_reader.integer(u"minHeight"_s, 4, 1, 32);
    m_feature.maxHeight = m_reader.integer(u"maxHeight"_s, m_feature.minHeight, m_feature.minHeight, 32);

    m_feature.shape = m_reader.string(u"shape"_s, u"round"_s);
    static const QStringList shapes{u"round"_s, u"cone"_s, u"bush"_s, u"giant_cone"_s, u"giant_round"_s};
    if (!shapes.contains(m_feature.shape)) {
        m_reader.fail(u"'shape' must be round, cone, bush, giant_cone or giant_round"_s);
    }
    m_feature.vinesBlock = m_reader.name(u"vines"_s, m_namespace);
    if (m_feature.trunkBlock.isEmpty()) {
        m_reader.fail(singleBlock ? u"'block' is required"_s : u"'trunk' is required"_s);
    }
    if (m_feature.type == QcFeatureDefinition::Type::Tree && m_feature.leavesBlock.isEmpty()) {
        m_reader.fail(u"trees need 'leaves'"_s);
    }

    const QJsonArray ground = m_json.value(u"ground"_s).toArray();
    for (const auto &entry : ground) {
        const QString name = QcContentNames::qualify(entry.toString(), m_namespace);
        if (name.isEmpty()) {
            m_reader.fail(u"invalid 'ground' entry"_s);
            break;
        }
        m_feature.groundBlocks.append(name);
    }

    if (!m_reader.ok()) {
        return qcError(m_reader.error);
    }
    return std::move(m_feature);
}


// The `features` a biome places with their densities.
[[nodiscard]] QList<QcFeaturePlacement> parsePlacements(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace)
{
    QList<QcFeaturePlacement> placements;
    const QJsonValue features = json.value(u"features"_s);
    if (!features.isUndefined() && !features.isArray()) {
        reader.fail(u"'features' must be an array"_s);
    }
    const QJsonArray featureList = features.toArray();
    if (featureList.size() > 64) {
        reader.fail(u"'features' has more than 64 entries"_s);
    }

    for (const auto &entry : featureList) {
        const QJsonObject placementJson = entry.toObject();
        QcJsonFieldReader placementReader{placementJson, reader.context, {}};
        const QcFeaturePlacement placement{placementReader.name(u"feature"_s, packNamespace),
                                           placementReader.number(u"density"_s, 0.0, 0.0, 1.0)};
        if (!entry.isObject() || placement.feature.isEmpty() || !placementReader.ok()) {
            reader.fail(placementReader.ok() ? u"feature entries need 'feature' and 'density'"_s : placementReader.error);
            break;
        }
        placements.append(placement);
    }
    return placements;
}

// Patches of a biome's ground, the field `key` (`underwaterPatches`, decision E-840;
// `surfacePatches`, decision E-850): up to 8 blocks with shares adding up to at most 1.
[[nodiscard]] QList<QcBlockShare> parseBlockShares(const QJsonObject &json, const QString &key, QcJsonFieldReader &reader,
                                                   const QString &packNamespace)
{
    QList<QcBlockShare> shares;
    const QJsonValue patches = json.value(key);
    if (!patches.isUndefined() && (!patches.isArray() || patches.toArray().size() > 8)) {
        reader.fail(u"'%1' must be an array of at most 8 entries"_s.arg(key));
        return shares;
    }

    double total = 0.0;
    const QJsonArray entries = patches.toArray();
    for (const auto &entry : entries) {
        const QJsonObject shareJson = entry.toObject();
        QcJsonFieldReader shareReader{shareJson, reader.context, {}};
        const QcBlockShare share{shareReader.name(u"block"_s, packNamespace), shareReader.number(u"share"_s, 0.0, 0.0, 1.0)};
        if (!entry.isObject() || share.block.isEmpty() || !shareJson.contains(u"share"_s) || !shareReader.ok()) {
            reader.fail(shareReader.ok() ? u"'%1' entries need 'block' and 'share'"_s.arg(key) : shareReader.error);
            break;
        }
        total += share.share;
        shares.append(share);
    }
    if (total > 1.0 + 1e-9) {
        reader.fail(u"the shares of '%1' add up to more than 1"_s.arg(key));
    }
    return shares;
}

// The `monsters` of a biome (decision E-838): {"zombie": 20, "husk": 80}, weights 0-100 in place of
// those of the kinds' spawn rules.
[[nodiscard]] QList<QcSpawnWeight> parseMonsterWeights(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace)
{
    QList<QcSpawnWeight> weights;
    const QJsonValue monsters = json.value(u"monsters"_s);
    if (!monsters.isUndefined() && (!monsters.isObject() || monsters.toObject().size() > 64)) {
        reader.fail(u"'monsters' must be an object of at most 64 creature names with weights"_s);
        return weights;
    }

    const QJsonObject entries = monsters.toObject();
    for (auto entry = entries.begin(); entry != entries.end(); ++entry) {
        const QString mob = QcContentNames::qualify(entry.key(), packNamespace);
        const double weight = entry.value().toDouble(-1.0);
        if (mob.isEmpty() || !entry.value().isDouble() || weight < 0.0 || weight > 100.0) {
            reader.fail(u"'monsters' entries need a creature name and a weight from 0 to 100"_s);
            break;
        }
        weights.append({mob, weight});
    }
    return weights;
}

} // namespace

namespace QcWorldgenParser {

[[nodiscard]] QcResult<QcBiomeDefinition> parseBiome(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"biome"_s, {}};
    QcBiomeDefinition biome;
    biome.name = reader.name(u"name"_s, packNamespace);
    if (biome.name.isEmpty()) {
        return qcError(reader.ok() ? u"biome: 'name' is required"_s : reader.error);
    }

    reader.context = u"biome '%1'"_s.arg(biome.name);
    biome.translationKey = reader.string(
        u"translation"_s, u"biome.%1.%2"_s.arg(biome.name.left(biome.name.indexOf(u':')), QcContentFields::localPart(biome.name)));
    biome.descriptionKey = reader.string(u"description"_s, biome.translationKey + u".description"_s);

    biome.temperature = reader.number(u"temperature"_s, 0.5, 0.0, 1.0);
    biome.humidity = reader.number(u"humidity"_s, 0.5, 0.0, 1.0);
    biome.ruggedness = reader.number(u"ruggedness"_s, biome.ruggedness, 0.0, 1.0);
    biome.rarity = reader.number(u"rarity"_s, biome.rarity, 0.0, 1.0);
    biome.spread = reader.number(u"spread"_s, biome.spread, 0.0, 0.1);
    biome.baseHeight = reader.number(u"baseHeight"_s, 68.0, 1.0, QcWorld::worldHeight - 40.0);
    biome.heightVariation = reader.number(u"heightVariation"_s, 6.0, 0.0, 120.0);
    biome.ridges = reader.number(u"ridges"_s, 0.0, 0.0, 1.0);
    biome.terraceHeight = reader.integer(u"terraceHeight"_s, 0, 0, 32);
    biome.overhang = reader.integer(u"overhang"_s, 0, 0, 16);

    biome.steepBlock = reader.name(u"steep"_s, packNamespace);
    biome.steepSlope = reader.integer(u"steepSlope"_s, biome.steepSlope, 1, 32);
    biome.surfaceBlock = reader.name(u"surface"_s, packNamespace);
    biome.subsurfaceBlock = reader.name(u"subsurface"_s, packNamespace, biome.surfaceBlock);
    biome.subsurfaceDepth = reader.integer(u"subsurfaceDepth"_s, 3, 0, 16);
    biome.underwaterBlock = reader.name(u"underwater"_s, packNamespace, biome.subsurfaceBlock);
    biome.beachBlock = reader.name(u"beach"_s, packNamespace, biome.surfaceBlock);
    biome.frozenWaterBlock = reader.name(u"frozenWater"_s, packNamespace);
    biome.snowLine = reader.integer(u"snowLine"_s, 0, 0, QcWorld::worldHeight);
    biome.snowBlock = reader.name(u"snow"_s, packNamespace);
    biome.villages = reader.boolean(u"villages"_s, false);
    biome.river = reader.boolean(u"river"_s, false);
    biome.riverBiome = reader.name(u"riverBiome"_s, packNamespace);
    biome.underwaterPatches = parseBlockShares(json, u"underwaterPatches"_s, reader, packNamespace);
    biome.monsterWeights = parseMonsterWeights(json, reader, packNamespace);
    biome.surfacePatches = parseBlockShares(json, u"surfacePatches"_s, reader, packNamespace);
    biome.frozenDepth = reader.integer(u"frozenDepth"_s, 0, 0, 16);
    biome.delta = reader.boolean(u"delta"_s, false);
    biome.deltaBiome = reader.name(u"deltaBiome"_s, packNamespace);
    if (biome.river && biome.delta) {
        reader.fail(u"a biome is either a river or a delta biome"_s);
    }
    reader.nestedObject(u"vegetation"_s, [&biome, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        biome.vegetation = QcVegetationParser::parse(nested, packNamespace);
    });

    if (biome.surfaceBlock.isEmpty()) {
        reader.fail(u"'surface' is required"_s);
    }
    if (biome.snowLine > 0 && biome.snowBlock.isEmpty()) {
        reader.fail(u"'snowLine' needs a 'snow' block"_s);
    }

    biome.features = parsePlacements(json, reader, packNamespace);

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return biome;
}

[[nodiscard]] QcResult<QcFeatureDefinition> parseFeature(const QJsonObject &json, const QString &packNamespace)
{
    return FeatureReader(json, packNamespace).read();
}

[[nodiscard]] QcResult<QcOreDefinition> parseOre(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"ore"_s, {}};
    QcOreDefinition ore;
    ore.name = reader.name(u"name"_s, packNamespace);
    if (ore.name.isEmpty()) {
        return qcError(reader.ok() ? u"ore: 'name' is required"_s : reader.error);
    }

    reader.context = u"ore '%1'"_s.arg(ore.name);
    ore.block = reader.name(u"block"_s, packNamespace);
    // Empty means the terrain's stone block.
    ore.replaces = reader.name(u"replaces"_s, packNamespace);
    ore.minY = reader.integer(u"minY"_s, 1, 0, QcWorld::worldHeight - 1);
    ore.maxY = reader.integer(u"maxY"_s, 64, ore.minY, QcWorld::worldHeight - 1);
    ore.veinsPerChunk = reader.integer(u"veinsPerChunk"_s, 8, 0, 256);
    ore.veinSize = reader.integer(u"veinSize"_s, 6, 1, 64);
    ore.biomes = QcContentFields::parseNames(u"biomes"_s, packNamespace, reader);

    if (ore.block.isEmpty()) {
        reader.fail(u"'block' is required"_s);
    }

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return ore;
}

namespace {

// The dungeons of the terrain (decision E-522); the blocks are checked by the generator.
[[nodiscard]] QcDungeonSettings parseDungeons(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace)
{
    using Settings = QcDungeonSettings;
    Settings dungeons;
    dungeons.enabled = reader.boolean(u"enabled"_s, true);
    dungeons.regionSize = reader.integer(u"regionSize"_s, dungeons.regionSize, 24, Settings::maxRegionSize);
    dungeons.chance = reader.number(u"chance"_s, dungeons.chance, 0.0, 1.0);
    dungeons.minY = reader.integer(u"minY"_s, dungeons.minY, 1, QcWorld::worldHeight - 24);
    dungeons.maxY = reader.integer(u"maxY"_s, dungeons.maxY, dungeons.minY, QcWorld::worldHeight - 24);
    dungeons.attempts = reader.integer(u"attempts"_s, dungeons.attempts, 1, 64);
    dungeons.maxCaveShare = reader.number(u"maxCaveShare"_s, dungeons.maxCaveShare, 0.0, 1.0);
    dungeons.minSize = reader.integer(u"minSize"_s, dungeons.minSize, 5, Settings::maxRoomSize);
    dungeons.maxSize = reader.integer(u"maxSize"_s, dungeons.maxSize, dungeons.minSize, Settings::maxRoomSize);
    dungeons.wall = reader.name(u"wall"_s, packNamespace);
    dungeons.moss = reader.name(u"moss"_s, packNamespace, dungeons.wall);
    dungeons.mossChance = reader.number(u"mossChance"_s, dungeons.mossChance, 0.0, 0.5);
    dungeons.spawner = reader.name(u"spawner"_s, packNamespace);
    dungeons.chest = reader.name(u"chest"_s, packNamespace);
    dungeons.lootTable = reader.name(u"lootTable"_s, packNamespace);
    dungeons.minChests = reader.integer(u"minChests"_s, dungeons.minChests, 0, 8);
    dungeons.maxChests = reader.integer(u"maxChests"_s, dungeons.maxChests, dungeons.minChests, 8);
    dungeons.corridors = reader.integer(u"corridors"_s, dungeons.corridors, 0, 4);
    dungeons.maxLength = reader.integer(u"maxLength"_s, dungeons.maxLength, 1, Settings::maxCorridorLength);
    dungeons.reach = reader.integer(u"reach"_s, dungeons.reach, 1, Settings::maxCorridorLength);
    dungeons.light = reader.name(u"light"_s, packNamespace);
    dungeons.lightSpacing = reader.integer(u"lightSpacing"_s, dungeons.lightSpacing, 2, Settings::maxCorridorLength);
    dungeons.lightDistance = reader.integer(u"lightDistance"_s, dungeons.lightDistance, 0, Settings::maxCorridorLength);
    const QJsonArray mobs = json.value(u"mobs"_s).toArray();
    for (const auto &entry : mobs) {
        const QJsonObject mob = entry.toObject();
        const QString name = mob.value(u"mob"_s).toString();
        const int weight = mob.value(u"weight"_s).toInt(1);
        if (name.isEmpty() || weight < 1) {
            reader.fail(u"'mobs' entries need a 'mob' and a positive 'weight'"_s);
            return dungeons;
        }
        dungeons.mobs.append({QcContentNames::qualify(name, packNamespace), weight});
    }
    const bool odd = dungeons.minSize % 2 == 1 && dungeons.maxSize % 2 == 1;
    if (reader.ok() && (!odd || dungeons.regionSize < dungeons.maxSize + 4 || dungeons.wall.isEmpty() || dungeons.spawner.isEmpty()
                        || dungeons.chest.isEmpty() || dungeons.light.isEmpty() || dungeons.mobs.isEmpty())) {
        reader.fail(u"needs odd room sizes that fit the region, 'wall', 'spawner', 'chest', 'light' and 'mobs'"_s);
    }
    return dungeons;
}

// The blocks, items and names of the mine shafts (decision E-570); the blocks are checked by the
// generator, the creature and the loot table with the spawners (QcSpawnerContent).
// Reads the mine shafts of the terrain (decision E-570): owns the settings being built.
class MineshaftReader
{
public:
    MineshaftReader(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace)
        : m_json(json)
        , m_reader(reader)
        , m_namespace(packNamespace)
    {
    }

    [[nodiscard]] QcMineshaftSettings read();

private:
    void parseMineshaftNames();

    const QJsonObject &m_json;
    QcJsonFieldReader &m_reader;
    const QString &m_namespace;
    QcMineshaftSettings m_shafts;
};

void MineshaftReader::parseMineshaftNames()
{
    m_shafts.planks = m_reader.name(u"planks"_s, m_namespace);
    m_shafts.post = m_reader.name(u"post"_s, m_namespace);
    m_shafts.rail = m_reader.name(u"rail"_s, m_namespace);
    m_shafts.cobweb = m_reader.name(u"cobweb"_s, m_namespace);
    m_shafts.light = m_reader.name(u"light"_s, m_namespace);
    m_shafts.ladder = m_reader.name(u"ladder"_s, m_namespace);
    m_shafts.seal = m_reader.name(u"seal"_s, m_namespace);
    m_shafts.spawner = m_reader.name(u"spawner"_s, m_namespace);
    m_shafts.mob = m_reader.name(u"mob"_s, m_namespace);
    m_shafts.cart = m_reader.name(u"cart"_s, m_namespace);
    m_shafts.lootTable = m_reader.name(u"lootTable"_s, m_namespace);
}

QcMineshaftSettings MineshaftReader::read()
{
    using Settings = QcMineshaftSettings;
    m_shafts.enabled = m_reader.boolean(u"enabled"_s, true);
    m_shafts.regionSize = m_reader.integer(u"regionSize"_s, m_shafts.regionSize, 32, Settings::maxRegionSize);
    m_shafts.chance = m_reader.number(u"chance"_s, m_shafts.chance, 0.0, 1.0);
    m_shafts.minY = m_reader.integer(u"minY"_s, m_shafts.minY, 2, QcWorld::worldHeight - 32);
    m_shafts.maxY = m_reader.integer(u"maxY"_s, m_shafts.maxY, m_shafts.minY, QcWorld::worldHeight - 32);
    m_shafts.attempts = m_reader.integer(u"attempts"_s, m_shafts.attempts, 1, 64);
    m_shafts.minLevels = m_reader.integer(u"minLevels"_s, m_shafts.minLevels, 1, Settings::maxLevelCount);
    m_shafts.maxLevels = m_reader.integer(u"maxLevels"_s, m_shafts.maxLevels, m_shafts.minLevels, Settings::maxLevelCount);
    m_shafts.levelSpacing = m_reader.integer(u"levelSpacing"_s, m_shafts.levelSpacing, 5, 12);
    m_shafts.radius = m_reader.integer(u"radius"_s, m_shafts.radius, 1, Settings::maxRadius);
    m_shafts.minNodes = m_reader.integer(u"minNodes"_s, m_shafts.minNodes, 1, 80);
    m_shafts.maxNodes = m_reader.integer(u"maxNodes"_s, m_shafts.maxNodes, m_shafts.minNodes, 80);
    m_shafts.maxRun = m_reader.integer(u"maxRun"_s, m_shafts.maxRun, 1, Settings::maxRunLength);
    m_shafts.loopChance = m_reader.number(u"loopChance"_s, m_shafts.loopChance, 0.0, 1.0);
    m_shafts.extraStairChance = m_reader.number(u"extraStairChance"_s, m_shafts.extraStairChance, 0.0, 1.0);
    m_shafts.roomHalf = m_reader.integer(u"roomHalf"_s, m_shafts.roomHalf, 1, 4);
    m_shafts.roomHeight = m_reader.integer(u"roomHeight"_s, m_shafts.roomHeight, 3, 8);
    m_shafts.groundCover = m_reader.integer(u"groundCover"_s, m_shafts.groundCover, 1, 32);
    m_shafts.maxPillar = m_reader.integer(u"maxPillar"_s, m_shafts.maxPillar, 1, 64);
    m_shafts.supportSpacing = m_reader.integer(u"supportSpacing"_s, m_shafts.supportSpacing, 2, 16);
    m_shafts.railChance = m_reader.number(u"railChance"_s, m_shafts.railChance, 0.0, 1.0);
    m_shafts.cobwebChance = m_reader.number(u"cobwebChance"_s, m_shafts.cobwebChance, 0.0, 1.0);
    m_shafts.lightChance = m_reader.number(u"lightChance"_s, m_shafts.lightChance, 0.0, 1.0);
    m_shafts.lightDistance = m_reader.integer(u"lightDistance"_s, m_shafts.lightDistance, 0, 32);
    m_shafts.minSpawners = m_reader.integer(u"minSpawners"_s, m_shafts.minSpawners, 0, 8);
    m_shafts.maxSpawners = m_reader.integer(u"maxSpawners"_s, m_shafts.maxSpawners, m_shafts.minSpawners, 8);
    m_shafts.cartChance = m_reader.number(u"cartChance"_s, m_shafts.cartChance, 0.0, 1.0);
    m_shafts.entranceChance = m_reader.number(u"entranceChance"_s, m_shafts.entranceChance, 0.0, 1.0);
    m_shafts.roomChance = m_reader.number(u"roomChance"_s, m_shafts.roomChance, 0.0, 1.0);
    m_shafts.maxRooms = m_reader.integer(u"maxRooms"_s, m_shafts.maxRooms, 0, 16);
    parseMineshaftNames();
    const QJsonObject biomes = m_json.value(u"entranceBiomes"_s).toObject();
    for (auto it = biomes.begin(); it != biomes.end(); ++it) {
        const double chance = it.value().toDouble(-1.0);
        if (chance < 0.0 || chance > 1.0) {
            m_reader.fail(u"'entranceBiomes' values are chances from 0 to 1"_s);
            return m_shafts;
        }
        m_shafts.entranceBiomes.append({QcContentNames::qualify(it.key(), m_namespace), chance});
    }
    const bool fits = m_shafts.regionSize >= 2 * m_shafts.extent() + 2 && m_shafts.maxY - (m_shafts.maxLevels - 1) * m_shafts.levelSpacing >= m_shafts.minY;
    const bool named = !m_shafts.planks.isEmpty() && !m_shafts.post.isEmpty() && !m_shafts.rail.isEmpty() && !m_shafts.cobweb.isEmpty()
                       && !m_shafts.light.isEmpty() && !m_shafts.ladder.isEmpty() && !m_shafts.seal.isEmpty() && !m_shafts.spawner.isEmpty()
                       && !m_shafts.mob.isEmpty() && !m_shafts.cart.isEmpty();
    if (m_reader.ok() && (!fits || !named)) {
        m_reader.fail(u"needs a network that fits the region and the heights, and 'planks', 'post', 'rail', 'cobweb', 'light', "
                    "'ladder', 'seal', 'spawner', 'mob' and 'cart'"_s);
    }
    return m_shafts;
}

} // namespace

[[nodiscard]] QcResult<QcTerrainSettings> parseTerrainSettings(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"terrain"_s, {}};
    QcTerrainSettings settings;
    settings.seaLevel = reader.integer(u"seaLevel"_s, QcWorld::seaLevel, 8, QcWorld::worldHeight - 32);
    settings.stoneBlock = reader.name(u"stone"_s, packNamespace);
    settings.waterBlock = reader.name(u"water"_s, packNamespace);
    settings.bedrockBlock = reader.name(u"bedrock"_s, packNamespace);
    settings.caves = reader.boolean(u"caves"_s, true);

    settings.caveTunnelWidth = reader.number(u"caveTunnelWidth"_s, settings.caveTunnelWidth, 0.0, 0.5);
    settings.caveCavernThreshold = reader.number(u"caveCavernThreshold"_s, settings.caveCavernThreshold, 0.0, 1.0);
    settings.caveLiquidBlock = reader.name(u"caveLiquid"_s, packNamespace);
    settings.caveLiquidLevel = reader.integer(u"caveLiquidLevel"_s, settings.caveLiquidLevel, 1, QcWorld::worldHeight - 1);
    settings.caveRoof = reader.integer(u"caveRoof"_s, settings.caveRoof, 0, 32);
    settings.caveEntrances = reader.number(u"caveEntrances"_s, settings.caveEntrances, 0.0, 1.0);
    settings.bedrockLayers = reader.integer(u"bedrockLayers"_s, settings.bedrockLayers, 1, 16);
    settings.vegetationBiome = reader.name(u"vegetationBiome"_s, packNamespace);
    settings.rivers = reader.boolean(u"rivers"_s, false);
    settings.riverBiome = reader.name(u"riverBiome"_s, packNamespace);
    settings.deltas = reader.boolean(u"deltas"_s, false);
    settings.deltaShare = reader.number(u"deltaShare"_s, settings.deltaShare, 0.0, 1.0);
    settings.deltaBiome = reader.name(u"deltaBiome"_s, packNamespace);
    reader.nestedObject(u"dungeons"_s, [&settings, &packNamespace](const QJsonObject &object, QcJsonFieldReader &nested) {
        settings.dungeons = parseDungeons(object, nested, packNamespace);
    });
    reader.nestedObject(u"mineshafts"_s, [&settings, &packNamespace](const QJsonObject &object, QcJsonFieldReader &nested) {
        settings.mineshafts = MineshaftReader(object, nested, packNamespace).read();
    });
    reader.nestedObject(u"freezing"_s, [&settings, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        QcColdEdgeSettings &edges = settings.coldEdges;
        edges.iceBlock = nested.name(u"block"_s, packNamespace);
        edges.temperature = nested.number(u"temperature"_s, edges.temperature, 0.0, 1.0);
        edges.riverTemperature = nested.number(u"riverTemperature"_s, edges.riverTemperature, 0.0, 1.0);
        edges.ragged = nested.number(u"ragged"_s, edges.ragged, 0.0, 0.5);
        if (nested.ok() && edges.iceBlock.isEmpty()) {
            nested.fail(u"'block' is required"_s);
        }
    });
    settings.coldEdges.groundBlend = reader.integer(u"groundBlend"_s, 0, 0, QcColdEdgeSettings::maxGroundBlend);
    reader.nestedObject(u"coastalTowers"_s, [&settings, &packNamespace](const QJsonObject &object, QcJsonFieldReader &nested) {
        settings.coastalTowers = QcCoastalTowerParser::parseCoastalTowers(object, nested, packNamespace);
    });

    if (settings.stoneBlock.isEmpty() || settings.waterBlock.isEmpty()) {
        reader.fail(u"'stone' and 'water' are required"_s);
    }

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return settings;
}

} // namespace QcWorldgenParser
