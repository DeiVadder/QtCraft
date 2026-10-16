#include "QcStructureParser.h"

#include "QcContentFields.h"
#include "QcContentNames.h"
#include "QcJsonFieldReader.h"
#include "QcStructureVariationParser.h"

#include <QJsonArray>
#include <QHash>

#include <cmath>
#include <optional>

using namespace Qt::StringLiterals;

namespace {

using Structure = QcStructureDefinition;

// Labels and creatures may sit a little outside the blocks (above a roof).
constexpr double positionMargin = 4.0;

[[nodiscard]] std::optional<QcVec3> readPoint(const QJsonValue &value, const Structure &structure)
{
    const QJsonArray array = value.toArray();
    if (!value.isArray() || array.size() != 3 || !array[0].isDouble() || !array[1].isDouble() || !array[2].isDouble()) {
        return std::nullopt;
    }

    const QcVec3 point{array[0].toDouble(), array[1].toDouble(), array[2].toDouble()};
    const auto inside = [](double v, int size) { return std::isfinite(v) && v >= -positionMargin && v <= size + positionMargin; };
    if (!inside(point.x, structure.sizeX) || !inside(point.y, structure.sizeY) || !inside(point.z, structure.sizeZ)) {
        return std::nullopt;
    }
    return point;
}

// A cell inside the structure.
[[nodiscard]] std::optional<QcBlockPos> readCell(const QJsonValue &value, const Structure &structure)
{
    const std::optional<QcVec3> point = readPoint(value, structure);
    if (!point || point->x != std::floor(point->x) || point->y != std::floor(point->y) || point->z != std::floor(point->z)) {
        return std::nullopt;
    }

    const QcBlockPos cell{int(point->x), int(point->y), int(point->z)};
    if (cell.x < 0 || cell.y < 0 || cell.z < 0 || cell.x >= structure.sizeX || cell.y >= structure.sizeY || cell.z >= structure.sizeZ) {
        return std::nullopt;
    }
    return cell;
}

// An optional array of at most maxExtras entries; fails the reader otherwise.
QJsonArray extraArray(const QJsonObject &json, const QString &key, QcJsonFieldReader &reader)
{
    const QJsonValue value = json.value(key);
    if (!value.isUndefined() && (!value.isArray() || value.toArray().size() > Structure::maxExtras)) {
        reader.fail(u"'%1' must be an array of at most %2 entries"_s.arg(key).arg(Structure::maxExtras));
        return {};
    }
    return value.toArray();
}

QHash<QChar, qint16> parsePalette(const QJsonObject &json, Structure &structure, QcJsonFieldReader &reader, const QString &packNamespace)
{
    QHash<QChar, qint16> keys;
    const QJsonObject palette = json.value(u"palette"_s).toObject();
    if (!json.value(u"palette"_s).isObject() || palette.isEmpty() || palette.size() > Structure::maxPaletteEntries) {
        reader.fail(u"'palette' must be an object with 1 to %1 entries"_s.arg(Structure::maxPaletteEntries));
        return keys;
    }

    for (auto it = palette.constBegin(); it != palette.constEnd(); ++it) {
        const QString state = it.value().isString() ? QcStructureParser::qualifiedStateName(it.value().toString(), packNamespace) : QString();
        if (it.key().size() != 1 || it.key() == u" "_s || state.isEmpty()) {
            reader.fail(u"palette entry '%1' needs a single character other than space and a block name"_s.arg(it.key()));
            return keys;
        }
        keys.insert(it.key().front(), qint16(structure.palette.size()));
        structure.palette.append(state);
    }
    return keys;
}

// The size of the layers: every layer has the same rows, every row the same length.
bool measureLayers(const QJsonArray &layers, Structure &structure)
{
    if (layers.isEmpty() || layers.size() > Structure::maxSize || !layers[0].isArray()) {
        return false;
    }

    structure.sizeY = int(layers.size());
    structure.sizeZ = int(layers[0].toArray().size());
    structure.sizeX = structure.sizeZ > 0 ? int(layers[0].toArray()[0].toString().size()) : 0;
    if (structure.sizeZ < 1 || structure.sizeZ > Structure::maxSize || structure.sizeX < 1 || structure.sizeX > Structure::maxSize) {
        return false;
    }

    for (const auto &layer : layers) {
        const QJsonArray rows = layer.toArray();
        if (!layer.isArray() || rows.size() != structure.sizeZ) {
            return false;
        }
        for (const auto &row : rows) {
            if (!row.isString() || row.toString().size() != structure.sizeX) {
                return false;
            }
        }
    }
    return true;
}

// One row of a layer (`start` is its first cell); a space keeps the cell.
bool parseRow(const QString &row, QcBlockPos start, const QHash<QChar, qint16> &keys, Structure &structure, QcJsonFieldReader &reader)
{
    for (int x = 0; x < structure.sizeX; ++x) {
        if (row[x] == u' ') {
            continue;
        }
        if (!keys.contains(row[x])) {
            reader.fail(u"layer %1 uses '%2', which the palette does not define"_s.arg(start.y).arg(row[x]));
            return false;
        }
        structure.cells[structure.cellIndex(x, start.y, start.z)] = keys.value(row[x]);
    }
    return true;
}

void parseLayers(const QJsonObject &json, const QHash<QChar, qint16> &keys, Structure &structure, QcJsonFieldReader &reader)
{
    const QJsonArray layers = json.value(u"layers"_s).toArray();
    if (!measureLayers(layers, structure)) {
        reader.fail(u"'layers' must be 1 to %1 layers of equally many rows of equal length (at most %1 each)"_s.arg(Structure::maxSize));
        return;
    }

    structure.cells.resize(qsizetype(structure.sizeX) * structure.sizeY * structure.sizeZ, Structure::keep);
    for (int y = 0; y < structure.sizeY; ++y) {
        const QJsonArray rows = layers[y].toArray();
        for (int z = 0; z < structure.sizeZ; ++z) {
            if (!parseRow(rows[z].toString(), {0, y, z}, keys, structure, reader)) {
                return;
            }
        }
    }
}

void parseMobs(const QJsonObject &json, Structure &structure, QcJsonFieldReader &reader, const QString &packNamespace)
{
    const QJsonArray mobs = extraArray(json, u"mobs"_s, reader);
    for (const auto &entry : mobs) {
        const QJsonObject object = entry.toObject();
        const QString mob = QcContentNames::qualify(object.value(u"mob"_s).toString(), packNamespace);
        const std::optional<QcVec3> at = readPoint(object.value(u"at"_s), structure);
        if (mob.isEmpty() || !at) {
            reader.fail(u"'mobs' entries need 'mob' and 'at' inside the structure"_s);
            return;
        }
        structure.mobs.append({mob, *at});
    }
}

void parseStands(const QJsonObject &json, Structure &structure, QcJsonFieldReader &reader)
{
    constexpr qsizetype maxProperties = 32;
    const QJsonArray stands = extraArray(json, u"stands"_s, reader);
    for (const auto &entry : stands) {
        const QJsonObject object = entry.toObject();
        const std::optional<QcVec3> at = readPoint(object.value(u"at"_s), structure);
        const double yaw = object.value(u"yaw"_s).toDouble(0.0);
        const QJsonArray properties = object.value(u"properties"_s).toArray();
        if (!at || !std::isfinite(yaw) || properties.size() > maxProperties) {
            reader.fail(u"'stands' entries need 'at' inside the structure, a 'yaw' and at most %1 'properties'"_s.arg(maxProperties));
            return;
        }

        Structure::Stand stand{*at, float(yaw), {}};
        for (const auto &property : properties) {
            stand.properties.append(property.toString().left(256));
        }
        structure.stands.append(stand);
    }
}

[[nodiscard]] std::optional<Structure::Stack> readStack(const QJsonValue &value, const QString &packNamespace)
{
    const QJsonObject object = value.toObject();
    const QString item = QcContentNames::qualify(object.value(u"item"_s).toString(), packNamespace);
    const QJsonValue count = object.value(u"count"_s);
    const int amount = count.isUndefined() ? 1 : count.toInt(0);
    if (!value.isObject() || item.isEmpty() || amount < 1 || amount > 64) {
        return std::nullopt;
    }
    return Structure::Stack{item, amount};
}

void parseContents(const QJsonObject &json, Structure &structure, QcJsonFieldReader &reader, const QString &packNamespace)
{
    const QJsonArray contents = extraArray(json, u"contents"_s, reader);
    for (const auto &entry : contents) {
        const QJsonObject object = entry.toObject();
        const std::optional<QcBlockPos> at = readCell(object.value(u"at"_s), structure);
        const QJsonArray items = object.value(u"items"_s).toArray();
        if (!at || items.size() > Structure::maxItemsPerContainer) {
            reader.fail(u"'contents' entries need 'at' inside the structure and at most %1 'items'"_s.arg(Structure::maxItemsPerContainer));
            return;
        }

        Structure::Contents placed{*at, {}};
        for (const auto &item : items) {
            const std::optional<Structure::Stack> stack = readStack(item, packNamespace);
            if (!stack) {
                reader.fail(u"'items' entries need 'item' and a 'count' of 1 to 64"_s);
                return;
            }
            placed.items.append(*stack);
        }
        structure.contents.append(placed);
    }
}

void parseLabels(const QJsonObject &json, Structure &structure, QcJsonFieldReader &reader)
{
    const QJsonArray labels = extraArray(json, u"labels"_s, reader);
    for (const auto &entry : labels) {
        const QJsonObject object = entry.toObject();
        const std::optional<QcVec3> at = readPoint(object.value(u"at"_s), structure);
        const QString text = object.value(u"text"_s).toString();
        const QString detail = object.value(u"detail"_s).toString();
        if (!at || text.isEmpty() || text.size() > 256 || detail.size() > 256) {
            reader.fail(u"'labels' entries need 'at' inside the structure and a 'text' (at most 256 characters)"_s);
            return;
        }
        structure.labels.append({*at, text, detail, object.value(u"heading"_s).toBool(false)});
    }
}

void parseCells(const QJsonObject &json, const QString &key, QList<QcBlockPos> &cells, const Structure &structure, QcJsonFieldReader &reader)
{
    const QJsonArray entries = extraArray(json, key, reader);
    for (const auto &entry : entries) {
        const std::optional<QcBlockPos> cell = readCell(entry, structure);
        if (!cell) {
            reader.fail(u"'%1' entries must be [x, y, z] cells inside the structure"_s.arg(key));
            return;
        }
        cells.append(*cell);
    }
}

// "minecarts": [x, y, z] for a plain cart or {"at": [x, y, z], "item": "chest_minecart"}.
void parseCarts(const QJsonObject &json, Structure &structure, QcJsonFieldReader &reader, const QString &packNamespace)
{
    const QJsonArray entries = extraArray(json, u"minecarts"_s, reader);
    for (const auto &entry : entries) {
        const QJsonObject object = entry.toObject();
        const std::optional<QcBlockPos> cell = readCell(entry.isObject() ? object.value(u"at"_s) : entry, structure);
        const QString item = QcContentNames::qualify(entry.isObject() ? object.value(u"item"_s).toString(u"qtcraft:minecart"_s) : u"qtcraft:minecart"_s,
                                                     packNamespace);
        if (!cell || item.isEmpty()) {
            reader.fail(u"'minecarts' entries must be [x, y, z] cells inside the structure or objects with 'at' and 'item'"_s);
            return;
        }
        structure.minecarts.append({*cell, item});
    }
}

} // namespace

namespace QcStructureParser {

QString qualifiedStateName(const QString &text, const QString &packNamespace)
{
    const qsizetype bracket = text.indexOf(u'[');
    const QString name = QcContentNames::qualify(bracket < 0 ? text : text.left(bracket), packNamespace);
    if (name.isEmpty() || (bracket >= 0 && !text.endsWith(u']'))) {
        return {};
    }
    return bracket < 0 ? name : name + text.mid(bracket);
}

[[nodiscard]] QcResult<QcStructureDefinition> parseStructure(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"structure"_s, {}};
    Structure structure;
    structure.name = reader.name(u"name"_s, packNamespace);
    if (structure.name.isEmpty()) {
        return qcError(reader.ok() ? u"structure: 'name' is required"_s : reader.error);
    }

    reader.context = u"structure '%1'"_s.arg(structure.name);
    structure.translationKey = reader.string(u"translation"_s, QcContentFields::translationKey(u"structure"_s, structure.name));
    reader.nestedObject(u"showcase"_s, [&structure](const QJsonObject &, QcJsonFieldReader &nested) {
        structure.showcase = true;
        structure.showcaseOrder = nested.integer(u"order"_s, 0, -10000, 10000);
    });

    const QHash<QChar, qint16> keys = parsePalette(json, structure, reader, packNamespace);
    if (reader.ok()) {
        parseLayers(json, keys, structure, reader);
    }
    if (reader.ok()) {
        parseMobs(json, structure, reader, packNamespace);
        parseStands(json, structure, reader);
        parseContents(json, structure, reader, packNamespace);
        parseLabels(json, structure, reader);
        parseCells(json, u"updates"_s, structure.updates, structure, reader);
        parseCells(json, u"growing"_s, structure.growing, structure, reader);
        parseCarts(json, structure, reader, packNamespace);
        QcStructureVariationParser::parseVariation(json, keys, structure, reader, packNamespace);
    }

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return structure;
}

} // namespace QcStructureParser
