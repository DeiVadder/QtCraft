#include "QcWorldgenParser.h"

#include "QcContentFields.h"
#include "QcContentNames.h"
#include "QcJsonFieldReader.h"
#include "QcWorldConstants.h"

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

    if (ore.block.isEmpty()) {
        reader.fail(u"'block' is required"_s);
    }

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return ore;
}

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

    if (settings.stoneBlock.isEmpty() || settings.waterBlock.isEmpty()) {
        reader.fail(u"'stone' and 'water' are required"_s);
    }

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return settings;
}

} // namespace QcWorldgenParser
