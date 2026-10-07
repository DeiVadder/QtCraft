#include "QcContentParser.h"

#include "QcContentNames.h"
#include "QcWorldConstants.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace {

struct FieldReader
{
    const QJsonObject &json;
    QString context;
    QString error;

    bool ok() const { return error.isEmpty(); }

    void fail(const QString &message)
    {
        if (error.isEmpty()) {
            error = context + u": "_s + message;
        }
    }

    QString string(const QString &key, const QString &fallback = {})
    {
        const QJsonValue value = json.value(key);
        if (value.isUndefined()) {
            return fallback;
        }
        if (!value.isString()) {
            fail(u"'%1' must be a string"_s.arg(key));
            return fallback;
        }
        return value.toString();
    }

    bool boolean(const QString &key, bool fallback)
    {
        const QJsonValue value = json.value(key);
        if (value.isUndefined()) {
            return fallback;
        }
        if (!value.isBool()) {
            fail(u"'%1' must be true or false"_s.arg(key));
            return fallback;
        }
        return value.toBool();
    }

    double number(const QString &key, double fallback, double low, double high)
    {
        const QJsonValue value = json.value(key);
        if (value.isUndefined()) {
            return fallback;
        }
        if (!value.isDouble() || value.toDouble() < low || value.toDouble() > high) {
            fail(u"'%1' must be a number within %2..%3"_s.arg(key).arg(low).arg(high));
            return fallback;
        }
        return value.toDouble();
    }

    int integer(const QString &key, int fallback, int low, int high)
    {
        const double value = number(key, fallback, low, high);
        if (value != double(int(value))) {
            fail(u"'%1' must be an integer"_s.arg(key));
            return fallback;
        }
        return int(value);
    }

    QString name(const QString &key, const QString &packNamespace, const QString &fallback = {})
    {
        QString raw = string(key, fallback);
        if (raw.isEmpty()) {
            return raw;
        }
        const QString qualified = QcContentNames::qualify(raw, packNamespace);
        if (qualified.isEmpty()) {
            fail(u"'%1' is not a valid name: '%2'"_s.arg(key, raw));
        }
        return qualified;
    }
};

std::optional<QcRenderLayer> renderLayerFromName(const QString &name)
{
    if (name == u"opaque"_s) {
        return QcRenderLayer::Opaque;
    }
    if (name == u"cutout"_s) {
        return QcRenderLayer::Cutout;
    }
    if (name == u"translucent"_s) {
        return QcRenderLayer::Translucent;
    }
    if (name == u"invisible"_s) {
        return QcRenderLayer::Invisible;
    }
    return std::nullopt;
}

QString localPart(const QString &qualifiedName)
{
    return qualifiedName.mid(qualifiedName.indexOf(u':') + 1);
}

} // namespace

namespace QcContentParser {

QcResult<QcBlockDefinition> parseBlock(const QJsonObject &json, const QString &packNamespace)
{
    FieldReader reader{json, u"block"_s, {}};
    QcBlockDefinition block;
    block.name = reader.name(u"name"_s, packNamespace);
    if (block.name.isEmpty()) {
        return qcError(reader.ok() ? u"block: 'name' is required"_s : reader.error);
    }
    reader.context = u"block '%1'"_s.arg(block.name);
    const QString blockNamespace = block.name.left(block.name.indexOf(u':'));

    block.translationKey = reader.string(u"translation"_s,
                                         u"block.%1.%2"_s.arg(blockNamespace, localPart(block.name).replace(u'/', u'.')));
    const QString layerName = reader.string(u"render"_s, u"opaque"_s);
    const std::optional<QcRenderLayer> layer = renderLayerFromName(layerName);
    if (!layer) {
        reader.fail(u"'render' must be opaque, cutout, translucent or invisible"_s);
    }
    block.renderLayer = layer.value_or(QcRenderLayer::Opaque);
    const bool fullCube = block.renderLayer == QcRenderLayer::Opaque;
    block.solid = reader.boolean(u"solid"_s, true);
    block.opaque = reader.boolean(u"opaque"_s, fullCube);
    block.liquid = reader.boolean(u"liquid"_s, false);
    block.replaceable = reader.boolean(u"replaceable"_s, false);
    block.lightOpacity = reader.integer(u"lightOpacity"_s, block.opaque ? 15 : (block.liquid ? 2 : 0), 0, 15);
    block.hardness = reader.number(u"hardness"_s, 1.0, -1.0, 1000.0);
    block.tool = reader.string(u"tool"_s);
    block.toolLevel = reader.integer(u"toolLevel"_s, 0, 0, 100);

    const QJsonValue drops = json.value(u"drops"_s);
    if (drops.isUndefined()) {
        block.drops = {{block.name, 1}};
    } else if (!drops.isArray()) {
        reader.fail(u"'drops' must be an array"_s);
    } else {
        const QJsonArray dropList = drops.toArray();
        for (const auto &entry : dropList) {
            if (!entry.isObject()) {
                reader.fail(u"'drops' entries must be objects"_s);
                break;
            }
            const QJsonObject dropJson = entry.toObject();
            FieldReader dropReader{dropJson, reader.context, {}};
            const QcItemDrop drop{dropReader.name(u"item"_s, packNamespace), dropReader.integer(u"count"_s, 1, 1, 64)};
            if (!dropReader.ok() || drop.item.isEmpty()) {
                reader.fail(dropReader.ok() ? u"drop needs an 'item'"_s : dropReader.error);
                break;
            }
            block.drops.append(drop);
        }
    }

    const QJsonValue textures = json.value(u"textures"_s);
    if (block.renderLayer != QcRenderLayer::Invisible) {
        if (!textures.isObject()) {
            reader.fail(u"'textures' object is required for visible blocks"_s);
        } else {
            const QJsonObject faces = textures.toObject();
            FieldReader textureReader{faces, reader.context + u" textures"_s, {}};
            const QString all = textureReader.name(u"all"_s, packNamespace);
            const QString side = textureReader.name(u"side"_s, packNamespace, all);
            const QString top = textureReader.name(u"top"_s, packNamespace, all);
            const QString bottom = textureReader.name(u"bottom"_s, packNamespace, all);
            const auto face = [&](const QString &key, const QString &fallback) {
                return textureReader.name(key, packNamespace, fallback);
            };
            block.faceTextures = {face(u"east"_s, side), face(u"west"_s, side), top,
                                  bottom, face(u"south"_s, side), face(u"north"_s, side)};
            if (!textureReader.ok()) {
                reader.fail(textureReader.error);
            }
            for (const QString &texture : block.faceTextures) {
                if (texture.isEmpty()) {
                    reader.fail(u"every face needs a texture (use 'all', 'side', 'top', 'bottom' or a face name)"_s);
                    break;
                }
            }
        }
    }

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return block;
}

QcResult<QcTextureDefinition> parseProceduralTexture(const QJsonObject &json, const QString &packNamespace)
{
    FieldReader reader{json, u"texture"_s, {}};
    QcTextureDefinition texture;
    texture.name = reader.name(u"name"_s, packNamespace);
    if (texture.name.isEmpty()) {
        return qcError(reader.ok() ? u"texture: 'name' is required"_s : reader.error);
    }
    if (!json.value(u"pattern"_s).isString()) {
        return qcError(u"texture '%1': 'pattern' string is required"_s.arg(texture.name));
    }
    texture.procedural = json;
    texture.procedural.remove(u"name"_s);
    return texture;
}

QcResult<QHash<QString, QString>> parseTranslations(const QJsonObject &json)
{
    QHash<QString, QString> entries;
    for (auto it = json.constBegin(); it != json.constEnd(); ++it) {
        if (!it.value().isString()) {
            return qcError(u"translation '%1' must be a string"_s.arg(it.key()));
        }
        entries.insert(it.key(), it.value().toString());
    }
    return entries;
}

QcResult<QcItemDefinition> parseItem(const QJsonObject &json, const QString &packNamespace)
{
    FieldReader reader{json, u"item"_s, {}};
    QcItemDefinition item;
    item.name = reader.name(u"name"_s, packNamespace);
    if (item.name.isEmpty()) {
        return qcError(reader.ok() ? u"item: 'name' is required"_s : reader.error);
    }
    reader.context = u"item '%1'"_s.arg(item.name);
    item.translationKey = reader.string(
        u"translation"_s, u"item.%1.%2"_s.arg(item.name.left(item.name.indexOf(u':')), localPart(item.name).replace(u'/', u'.')));
    item.texture = reader.name(u"texture"_s, packNamespace, item.name);
    item.maxStack = reader.integer(u"maxStack"_s, 64, 1, 999);
    item.placesBlock = reader.name(u"places"_s, packNamespace);
    item.tool = reader.string(u"tool"_s);
    item.toolLevel = reader.integer(u"toolLevel"_s, 0, 0, 100);
    item.toolSpeed = reader.number(u"toolSpeed"_s, 1.0, 0.1, 100.0);
    item.durability = reader.integer(u"durability"_s, 0, 0, 100000);
    item.attackDamage = reader.number(u"attackDamage"_s, 1.0, 0.0, 1000.0);
    if (!item.tool.isEmpty() && item.maxStack != 1 && !json.contains(u"maxStack"_s)) {
        item.maxStack = 1;
    }
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return item;
}

QcResult<QcBiomeDefinition> parseBiome(const QJsonObject &json, const QString &packNamespace)
{
    FieldReader reader{json, u"biome"_s, {}};
    QcBiomeDefinition biome;
    biome.name = reader.name(u"name"_s, packNamespace);
    if (biome.name.isEmpty()) {
        return qcError(reader.ok() ? u"biome: 'name' is required"_s : reader.error);
    }
    reader.context = u"biome '%1'"_s.arg(biome.name);
    biome.translationKey = reader.string(
        u"translation"_s, u"biome.%1.%2"_s.arg(biome.name.left(biome.name.indexOf(u':')), localPart(biome.name)));
    biome.temperature = reader.number(u"temperature"_s, 0.5, 0.0, 1.0);
    biome.humidity = reader.number(u"humidity"_s, 0.5, 0.0, 1.0);
    biome.baseHeight = reader.number(u"baseHeight"_s, 68.0, 1.0, QcWorld::worldHeight - 40.0);
    biome.heightVariation = reader.number(u"heightVariation"_s, 6.0, 0.0, 120.0);
    biome.surfaceBlock = reader.name(u"surface"_s, packNamespace);
    biome.subsurfaceBlock = reader.name(u"subsurface"_s, packNamespace, biome.surfaceBlock);
    biome.subsurfaceDepth = reader.integer(u"subsurfaceDepth"_s, 3, 0, 16);
    biome.underwaterBlock = reader.name(u"underwater"_s, packNamespace, biome.subsurfaceBlock);
    biome.beachBlock = reader.name(u"beach"_s, packNamespace, biome.surfaceBlock);
    biome.frozenWaterBlock = reader.name(u"frozenWater"_s, packNamespace);
    biome.snowLine = reader.integer(u"snowLine"_s, 0, 0, QcWorld::worldHeight);
    biome.snowBlock = reader.name(u"snow"_s, packNamespace);
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
    for (const auto &entry : featureList) {
        const QJsonObject placementJson = entry.toObject();
        FieldReader placementReader{placementJson, reader.context, {}};
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

QcResult<QcFeatureDefinition> parseFeature(const QJsonObject &json, const QString &packNamespace)
{
    FieldReader reader{json, u"feature"_s, {}};
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
    } else {
        reader.fail(u"'type' must be tree or column"_s);
    }
    feature.trunkBlock = reader.name(u"trunk"_s, packNamespace);
    feature.leavesBlock = reader.name(u"leaves"_s, packNamespace);
    feature.minHeight = reader.integer(u"minHeight"_s, 4, 1, 32);
    feature.maxHeight = reader.integer(u"maxHeight"_s, feature.minHeight, feature.minHeight, 32);
    feature.shape = reader.string(u"shape"_s, u"round"_s);
    if (feature.shape != u"round"_s && feature.shape != u"cone"_s) {
        reader.fail(u"'shape' must be round or cone"_s);
    }
    if (feature.trunkBlock.isEmpty()) {
        reader.fail(u"'trunk' is required"_s);
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

QcResult<QcOreDefinition> parseOre(const QJsonObject &json, const QString &packNamespace)
{
    FieldReader reader{json, u"ore"_s, {}};
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

QcResult<QcTerrainSettings> parseTerrainSettings(const QJsonObject &json, const QString &packNamespace)
{
    FieldReader reader{json, u"terrain"_s, {}};
    QcTerrainSettings settings;
    settings.seaLevel = reader.integer(u"seaLevel"_s, QcWorld::seaLevel, 8, QcWorld::worldHeight - 32);
    settings.stoneBlock = reader.name(u"stone"_s, packNamespace);
    settings.waterBlock = reader.name(u"water"_s, packNamespace);
    settings.bedrockBlock = reader.name(u"bedrock"_s, packNamespace);
    settings.caves = reader.boolean(u"caves"_s, true);
    settings.caveTunnelWidth = reader.number(u"caveTunnelWidth"_s, settings.caveTunnelWidth, 0.0, 0.5);
    settings.caveCavernThreshold = reader.number(u"caveCavernThreshold"_s, settings.caveCavernThreshold, 0.0, 1.0);
    if (settings.stoneBlock.isEmpty() || settings.waterBlock.isEmpty()) {
        reader.fail(u"'stone' and 'water' are required"_s);
    }
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return settings;
}

} // namespace QcContentParser
