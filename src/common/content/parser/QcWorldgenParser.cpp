#include "common/content/parser/QcWorldgenParser.h"

#include "common/content/parser/QcContentFields.h"
#include "common/content/parser/QcJsonFieldReader.h"
#include "common/content/registry/QcContentNames.h"
#include "common/core/QcWorldConstants.h"

#include <QJsonArray>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

constexpr int maxPoolAttempts = 8;

void parsePool(const QJsonObject &json, QcFeatureDefinition &feature, QcJsonFieldReader &reader, const QString &packNamespace)
{
    feature.minY = reader.integer(u"minY"_s, feature.minY, 1, QcWorld::worldHeight - 2);
    feature.maxY = reader.integer(u"maxY"_s, std::max(feature.minY, feature.maxY), feature.minY, QcWorld::worldHeight - 2);
    feature.attempts = reader.integer(u"attempts"_s, feature.attempts, 1, maxPoolAttempts);
    feature.chance = reader.number(u"chance"_s, feature.chance, 0.0, 1.0);
    feature.radius = reader.integer(u"radius"_s, feature.radius, 1, 6);
    feature.depth = reader.integer(u"depth"_s, feature.depth, 1, 4);
    feature.surface = reader.boolean(u"surface"_s, false);

    const QJsonValue biomes = json.value(u"biomes"_s);
    if (!biomes.isUndefined() && (!biomes.isArray() || biomes.toArray().size() > 64)) {
        reader.fail(u"'biomes' must be an array of at most 64 biome names"_s);
    }
    const QJsonArray biomeList = biomes.toArray();
    for (const auto &entry : biomeList) {
        const QString name = entry.isString() ? QcContentNames::qualify(entry.toString(), packNamespace) : QString();
        if (name.isEmpty()) {
            reader.fail(u"invalid 'biomes' entry"_s);
            break;
        }
        feature.biomes.append(name);
    }
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

    if (biome.surfaceBlock.isEmpty()) {
        reader.fail(u"'surface' is required"_s);
    }
    if (biome.snowLine > 0 && biome.snowBlock.isEmpty()) {
        reader.fail(u"'snowLine' needs a 'snow' block"_s);
    }

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
        biome.features.append(placement);
    }

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return biome;
}

[[nodiscard]] QcResult<QcFeatureDefinition> parseFeature(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"feature"_s, {}};
    QcFeatureDefinition feature;
    feature.name = reader.name(u"name"_s, packNamespace);
    if (feature.name.isEmpty()) {
        return qcError(reader.ok() ? u"feature: 'name' is required"_s : reader.error);
    }

    reader.context = u"feature '%1'"_s.arg(feature.name);
    const QString type = reader.string(u"type"_s, u"tree"_s);
    if (type == u"tree"_s) {
        feature.type = QcFeatureDefinition::Type::Tree;
    } else if (type == u"column"_s) {
        feature.type = QcFeatureDefinition::Type::Column;
    } else if (type == u"plant"_s) {
        feature.type = QcFeatureDefinition::Type::Plant;
    } else if (type == u"pool"_s) {
        feature.type = QcFeatureDefinition::Type::Pool;
    } else {
        reader.fail(u"'type' must be tree, column, plant or pool"_s);
    }

    const bool singleBlock = feature.type == QcFeatureDefinition::Type::Plant || feature.type == QcFeatureDefinition::Type::Pool;
    feature.trunkBlock = reader.name(singleBlock ? u"block"_s : u"trunk"_s, packNamespace);
    if (feature.type == QcFeatureDefinition::Type::Pool) {
        parsePool(json, feature, reader, packNamespace);
    }

    feature.nearWater = reader.boolean(u"nearWater"_s, false);
    feature.patchSize = reader.integer(u"patchSize"_s, 0, 0, 512);
    feature.leavesBlock = reader.name(u"leaves"_s, packNamespace);
    feature.minHeight = reader.integer(u"minHeight"_s, 4, 1, 32);
    feature.maxHeight = reader.integer(u"maxHeight"_s, feature.minHeight, feature.minHeight, 32);

    feature.shape = reader.string(u"shape"_s, u"round"_s);
    if (feature.shape != u"round"_s && feature.shape != u"cone"_s) {
        reader.fail(u"'shape' must be round or cone"_s);
    }
    if (feature.trunkBlock.isEmpty()) {
        reader.fail(singleBlock ? u"'block' is required"_s : u"'trunk' is required"_s);
    }
    if (feature.type == QcFeatureDefinition::Type::Tree && feature.leavesBlock.isEmpty()) {
        reader.fail(u"trees need 'leaves'"_s);
    }

    const QJsonArray ground = json.value(u"ground"_s).toArray();
    for (const auto &entry : ground) {
        const QString name = QcContentNames::qualify(entry.toString(), packNamespace);
        if (name.isEmpty()) {
            reader.fail(u"invalid 'ground' entry"_s);
            break;
        }
        feature.groundBlocks.append(name);
    }

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return feature;
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
void parseDungeons(const QJsonObject &json, QcDungeonSettings &dungeons, QcJsonFieldReader &reader, const QString &packNamespace)
{
    using Settings = QcDungeonSettings;
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
            return;
        }
        dungeons.mobs.append({QcContentNames::qualify(name, packNamespace), weight});
    }
    const bool odd = dungeons.minSize % 2 == 1 && dungeons.maxSize % 2 == 1;
    if (reader.ok() && (!odd || dungeons.regionSize < dungeons.maxSize + 4 || dungeons.wall.isEmpty() || dungeons.spawner.isEmpty()
                        || dungeons.chest.isEmpty() || dungeons.light.isEmpty() || dungeons.mobs.isEmpty())) {
        reader.fail(u"needs odd room sizes that fit the region, 'wall', 'spawner', 'chest', 'light' and 'mobs'"_s);
    }
}

// The blocks, items and names of the mine shafts (decision E-570); the blocks are checked by the
// generator, the creature and the loot table with the spawners (QcSpawnerContent).
void parseMineshaftNames(QcMineshaftSettings &shafts, QcJsonFieldReader &reader, const QString &packNamespace)
{
    shafts.planks = reader.name(u"planks"_s, packNamespace);
    shafts.post = reader.name(u"post"_s, packNamespace);
    shafts.rail = reader.name(u"rail"_s, packNamespace);
    shafts.cobweb = reader.name(u"cobweb"_s, packNamespace);
    shafts.light = reader.name(u"light"_s, packNamespace);
    shafts.ladder = reader.name(u"ladder"_s, packNamespace);
    shafts.seal = reader.name(u"seal"_s, packNamespace);
    shafts.spawner = reader.name(u"spawner"_s, packNamespace);
    shafts.mob = reader.name(u"mob"_s, packNamespace);
    shafts.cart = reader.name(u"cart"_s, packNamespace);
    shafts.lootTable = reader.name(u"lootTable"_s, packNamespace);
}

// The mine shafts of the terrain (decision E-570).
void parseMineshafts(const QJsonObject &json, QcMineshaftSettings &shafts, QcJsonFieldReader &reader, const QString &packNamespace)
{
    using Settings = QcMineshaftSettings;
    shafts.enabled = reader.boolean(u"enabled"_s, true);
    shafts.regionSize = reader.integer(u"regionSize"_s, shafts.regionSize, 32, Settings::maxRegionSize);
    shafts.chance = reader.number(u"chance"_s, shafts.chance, 0.0, 1.0);
    shafts.minY = reader.integer(u"minY"_s, shafts.minY, 2, QcWorld::worldHeight - 32);
    shafts.maxY = reader.integer(u"maxY"_s, shafts.maxY, shafts.minY, QcWorld::worldHeight - 32);
    shafts.attempts = reader.integer(u"attempts"_s, shafts.attempts, 1, 64);
    shafts.minLevels = reader.integer(u"minLevels"_s, shafts.minLevels, 1, Settings::maxLevelCount);
    shafts.maxLevels = reader.integer(u"maxLevels"_s, shafts.maxLevels, shafts.minLevels, Settings::maxLevelCount);
    shafts.levelSpacing = reader.integer(u"levelSpacing"_s, shafts.levelSpacing, 5, 12);
    shafts.radius = reader.integer(u"radius"_s, shafts.radius, 1, Settings::maxRadius);
    shafts.minNodes = reader.integer(u"minNodes"_s, shafts.minNodes, 1, 80);
    shafts.maxNodes = reader.integer(u"maxNodes"_s, shafts.maxNodes, shafts.minNodes, 80);
    shafts.loopChance = reader.number(u"loopChance"_s, shafts.loopChance, 0.0, 1.0);
    shafts.extraStairChance = reader.number(u"extraStairChance"_s, shafts.extraStairChance, 0.0, 1.0);
    shafts.roomHalf = reader.integer(u"roomHalf"_s, shafts.roomHalf, 1, 4);
    shafts.roomHeight = reader.integer(u"roomHeight"_s, shafts.roomHeight, 3, 8);
    shafts.groundCover = reader.integer(u"groundCover"_s, shafts.groundCover, 1, 32);
    shafts.maxPillar = reader.integer(u"maxPillar"_s, shafts.maxPillar, 1, 64);
    shafts.supportSpacing = reader.integer(u"supportSpacing"_s, shafts.supportSpacing, 2, 16);
    shafts.railChance = reader.number(u"railChance"_s, shafts.railChance, 0.0, 1.0);
    shafts.cobwebChance = reader.number(u"cobwebChance"_s, shafts.cobwebChance, 0.0, 1.0);
    shafts.lightChance = reader.number(u"lightChance"_s, shafts.lightChance, 0.0, 1.0);
    shafts.lightDistance = reader.integer(u"lightDistance"_s, shafts.lightDistance, 0, 32);
    shafts.minSpawners = reader.integer(u"minSpawners"_s, shafts.minSpawners, 0, 8);
    shafts.maxSpawners = reader.integer(u"maxSpawners"_s, shafts.maxSpawners, shafts.minSpawners, 8);
    shafts.cartChance = reader.number(u"cartChance"_s, shafts.cartChance, 0.0, 1.0);
    shafts.entranceChance = reader.number(u"entranceChance"_s, shafts.entranceChance, 0.0, 1.0);
    shafts.roomChance = reader.number(u"roomChance"_s, shafts.roomChance, 0.0, 1.0);
    shafts.maxRooms = reader.integer(u"maxRooms"_s, shafts.maxRooms, 0, 16);
    parseMineshaftNames(shafts, reader, packNamespace);
    const QJsonObject biomes = json.value(u"entranceBiomes"_s).toObject();
    for (auto it = biomes.begin(); it != biomes.end(); ++it) {
        const double chance = it.value().toDouble(-1.0);
        if (chance < 0.0 || chance > 1.0) {
            reader.fail(u"'entranceBiomes' values are chances from 0 to 1"_s);
            return;
        }
        shafts.entranceBiomes.append({QcContentNames::qualify(it.key(), packNamespace), chance});
    }
    const bool fits = shafts.regionSize >= 2 * shafts.extent() + 2 && shafts.maxY - (shafts.maxLevels - 1) * shafts.levelSpacing >= shafts.minY;
    const bool named = !shafts.planks.isEmpty() && !shafts.post.isEmpty() && !shafts.rail.isEmpty() && !shafts.cobweb.isEmpty()
                       && !shafts.light.isEmpty() && !shafts.ladder.isEmpty() && !shafts.seal.isEmpty() && !shafts.spawner.isEmpty()
                       && !shafts.mob.isEmpty() && !shafts.cart.isEmpty();
    if (reader.ok() && (!fits || !named)) {
        reader.fail(u"needs a network that fits the region and the heights, and 'planks', 'post', 'rail', 'cobweb', 'light', "
                    "'ladder', 'seal', 'spawner', 'mob' and 'cart'"_s);
    }
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
    reader.nestedObject(u"dungeons"_s, [&settings, &packNamespace](const QJsonObject &object, QcJsonFieldReader &nested) {
        parseDungeons(object, settings.dungeons, nested, packNamespace);
    });
    reader.nestedObject(u"mineshafts"_s, [&settings, &packNamespace](const QJsonObject &object, QcJsonFieldReader &nested) {
        parseMineshafts(object, settings.mineshafts, nested, packNamespace);
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
