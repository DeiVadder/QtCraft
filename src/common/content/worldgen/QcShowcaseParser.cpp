// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/worldgen/QcShowcaseParser.h"

#include "common/content/registry/QcContentNames.h"
#include "common/content/registry/QcJsonFieldReader.h"
#include "common/core/QcWorldConstants.h"
#include "common/world/QcDayCycle.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace {

using Showcase = QcShowcaseDefinition;

// Room left above the ground for buildings and lookouts.
constexpr int headroom = 64;

[[nodiscard]] std::optional<bool> optionalBoolean(const QJsonObject &json, const QString &key, QcJsonFieldReader &reader)
{
    const QJsonValue value = json.value(key);
    if (value.isUndefined()) {
        return std::nullopt;
    }
    if (!value.isBool()) {
        reader.fail(u"'%1' must be true or false"_s.arg(key));
        return std::nullopt;
    }
    return value.toBool();
}

QStringList stringList(const QJsonObject &json, const QString &key, QcJsonFieldReader &reader)
{
    const QJsonValue value = json.value(key);
    QStringList result;
    if (value.isUndefined()) {
        return result;
    }

    const QJsonArray entries = value.toArray();
    if (!value.isArray() || entries.size() > 64) {
        reader.fail(u"'%1' must be an array of at most 64 texts"_s.arg(key));
        return result;
    }
    for (const auto &entry : entries) {
        if (!entry.isString() || entry.toString().isEmpty()) {
            reader.fail(u"'%1' entries must be texts"_s.arg(key));
            return {};
        }
        result.append(entry.toString());
    }
    return result;
}

QcShowcaseMatch parseMatch(const QJsonObject &json, QcJsonFieldReader &reader)
{
    QcShowcaseMatch match;
    match.liquid = optionalBoolean(json, u"liquid"_s, reader);
    match.part = optionalBoolean(json, u"part"_s, reader);
    match.light = optionalBoolean(json, u"light"_s, reader);
    match.plant = optionalBoolean(json, u"plant"_s, reader);
    match.functional = optionalBoolean(json, u"functional"_s, reader);
    match.falls = optionalBoolean(json, u"falls"_s, reader);
    match.signal = optionalBoolean(json, u"signal"_s, reader);
    match.render = stringList(json, u"render"_s, reader);
    match.tools = stringList(json, u"tool"_s, reader);
    match.nameContains = stringList(json, u"nameContains"_s, reader);
    return match;
}

[[nodiscard]] QList<QcShowcaseCategory> parseCategories(const QJsonObject &json, QcJsonFieldReader &reader)
{
    QList<QcShowcaseCategory> result;
    const QJsonArray categories = json.value(u"categories"_s).toArray();
    if (categories.isEmpty() || categories.size() > Showcase::maxCategories) {
        reader.fail(u"'categories' must be an array of 1 to %1 categories"_s.arg(Showcase::maxCategories));
        return result;
    }

    for (const auto &entry : categories) {
        const QJsonObject object = entry.toObject();
        QcJsonFieldReader categoryReader{object, reader.context + u" category"_s, {}};
        QcShowcaseCategory category;
        category.name = categoryReader.string(u"name"_s);
        if (!QcContentNames::isValidPart(category.name)) {
            reader.fail(u"categories need a 'name' of lower-case letters, digits and '_'"_s);
            return result;
        }

        category.translationKey = categoryReader.string(u"translation"_s, u"showcase.category."_s + category.name);
        categoryReader.nestedObject(u"match"_s, [&category](const QJsonObject &match, QcJsonFieldReader &nested) {
            category.match = parseMatch(match, nested);
        });
        reader.adopt(categoryReader);
        result.append(category);
    }
    return result;
}

[[nodiscard]] QList<Showcase::Layer> parseLayers(const QJsonObject &json, QcJsonFieldReader &reader, const QString &packNamespace)
{
    QList<Showcase::Layer> result;
    const QJsonArray layers = json.value(u"layers"_s).toArray();
    if (layers.isEmpty() || layers.size() > Showcase::maxLayers) {
        reader.fail(u"'layers' must be an array of 1 to %1 layers"_s.arg(Showcase::maxLayers));
        return result;
    }

    int height = 0;
    for (const auto &entry : layers) {
        const QJsonObject object = entry.toObject();
        QcJsonFieldReader layerReader{object, reader.context + u" layer"_s, {}};
        const Showcase::Layer layer{layerReader.name(u"block"_s, packNamespace), layerReader.integer(u"depth"_s, 1, 1, QcWorld::worldHeight)};
        reader.adopt(layerReader);
        if (layer.block.isEmpty()) {
            reader.fail(u"layers need a 'block'"_s);
            return result;
        }
        height += layer.depth;
        result.append(layer);
    }

    if (height > QcWorld::worldHeight - headroom) {
        reader.fail(u"the layers are %1 blocks high, at most %2 leave room above"_s.arg(height).arg(QcWorld::worldHeight - headroom));
    }
    return result;
}

} // namespace

namespace QcShowcaseParser {

[[nodiscard]] QcResult<QcShowcaseDefinition> parseShowcase(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"showcase"_s, {}};
    Showcase showcase;
    showcase.timeOfDay = reader.number(u"timeOfDay"_s, showcase.timeOfDay, 0.0, QcDayCycle::dayLength);
    showcase.podium = reader.name(u"podium"_s, packNamespace, u"stone"_s);
    showcase.enclosure = reader.name(u"enclosure"_s, packNamespace, u"glass"_s);
    showcase.container = reader.name(u"container"_s, packNamespace, u"chest"_s);
    showcase.lookout = reader.name(u"lookout"_s, packNamespace);
    showcase.rowWidth = reader.integer(u"rowWidth"_s, showcase.rowWidth, 32, 1024);

    showcase.layers = parseLayers(json, reader, packNamespace);
    if (reader.ok()) {
        showcase.categories = parseCategories(json, reader);
    }
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return showcase;
}

} // namespace QcShowcaseParser
