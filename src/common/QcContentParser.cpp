#include "QcContentParser.h"

#include "QcContentNames.h"

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

} // namespace QcContentParser
