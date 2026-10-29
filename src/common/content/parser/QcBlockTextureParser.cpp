// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/parser/QcBlockTextureParser.h"

#include "common/content/parser/QcJsonFieldReader.h"
#include "common/content/registry/QcContentNames.h"

using namespace Qt::StringLiterals;

namespace {

// The texture slots a shape part may use: its slot, or for "$<property>" the slot of every value
// of that property (decision E-765).
QStringList partSlots(const QcBlockDefinition &block, const QcShapePart &part)
{
    if (!part.texture.startsWith(u'$')) {
        return {part.texture};
    }
    const QcBlockProperty *property = block.property(part.texture.mid(1));
    return property ? property->values : QStringList{part.texture};
}

QList<std::pair<QString, QString>> parseSlots(const QJsonObject &faces, QcJsonFieldReader &textureReader, const QString &packNamespace)
{
    const QJsonObject slotJson = faces.value(u"slots"_s).toObject();
    if (slotJson.size() > 16 || (faces.contains(u"slots"_s) && !faces.value(u"slots"_s).isObject())) {
        textureReader.fail(u"'slots' must be an object of at most 16 named textures"_s);
    }

    QList<std::pair<QString, QString>> namedSlots;
    for (auto it = slotJson.constBegin(); it != slotJson.constEnd(); ++it) {
        const QString texture = it.value().isString() ? QcContentNames::qualify(it.value().toString(), packNamespace) : QString();
        if (texture.isEmpty() || it.key().isEmpty() || it.key().size() > 32) {
            textureReader.fail(u"invalid texture slot '%1'"_s.arg(it.key()));
            break;
        }
        namedSlots.append({it.key(), texture});
    }
    return namedSlots;
}

} // namespace

namespace QcBlockTextureParser {

QcBlockTextures parseTextures(const QJsonObject &blockJson, QcJsonFieldReader &reader, const QString &packNamespace)
{
    const QJsonValue textures = blockJson.value(u"textures"_s);
    if (!textures.isObject()) {
        reader.fail(u"'textures' object is required for visible blocks"_s);
        return {};
    }

    const QJsonObject faces = textures.toObject();
    QcJsonFieldReader textureReader{faces, reader.context + u" textures"_s, {}};
    const QString all = textureReader.name(u"all"_s, packNamespace);
    const QString side = textureReader.name(u"side"_s, packNamespace, all);
    const QString top = textureReader.name(u"top"_s, packNamespace, all);
    const QString bottom = textureReader.name(u"bottom"_s, packNamespace, all);
    const auto face = [&](const QString &key, const QString &fallback) {
        return textureReader.name(key, packNamespace, fallback);
    };
    QcBlockTextures result;
    result.faces = {face(u"east"_s, side), face(u"west"_s, side), top,
                    bottom, face(u"south"_s, side), face(u"north"_s, side)};
    result.namedSlots = parseSlots(faces, textureReader, packNamespace);
    reader.adopt(textureReader);

    for (const QString &texture : result.faces) {
        if (texture.isEmpty()) {
            reader.fail(u"every face needs a texture (use 'all', 'side', 'top', 'bottom' or a face name)"_s);
            break;
        }
    }
    return result;
}

void checkPartTextures(const QcBlockDefinition &block, QcJsonFieldReader &reader)
{
    if (block.renderLayer == QcRenderLayer::Invisible) {
        return;
    }
    for (const QcShapePart &part : std::as_const(block.shape.parts)) {
        const QStringList named = partSlots(block, part);
        for (const QString &slot : named) {
            if (!slot.isEmpty() && block.slotTexture(slot).isEmpty()) {
                reader.fail(u"shape part uses the unknown texture slot '%1'"_s.arg(slot));
                return;
            }
        }
    }
}

} // namespace QcBlockTextureParser
