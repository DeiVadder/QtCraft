#include "common/content/parser/QcContentParser.h"

#include "common/content/definitions/QcHangingDefinition.h"
#include "common/content/parser/QcContentFields.h"
#include "common/content/parser/QcJsonFieldReader.h"
#include "common/content/registry/QcContentNames.h"
#include "common/protocol/QcStreamLimits.h"

#include <QJsonArray>
#include <QRegularExpression>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

// Emblem textures (decision E-241): at most 16 rows of 16 characters and 16 colours, so the
// definition stays small; the client checks the colours when it paints.
constexpr qsizetype maxBitmapSide = 16;

bool isBitmap(const QJsonValue &value)
{
    const QJsonArray rows = value.toArray();
    return value.isArray() && rows.size() <= maxBitmapSide
           && std::ranges::all_of(rows, [](const QJsonValue &row) { return row.isString() && row.toString().size() <= maxBitmapSide; });
}

bool isPalette(const QJsonValue &value)
{
    const QJsonObject palette = value.toObject();
    return value.isObject() && palette.size() <= maxBitmapSide && std::ranges::all_of(palette.keys(), [&palette](const QString &key) {
               return key.size() == 1 && palette.value(key).isString() && palette.value(key).toString().size() <= 9;
           });
}

// "tiles": [width, height] of a texture drawn over several blocks (decision E-791).
bool isTiles(const QJsonValue &value)
{
    const QJsonArray tiles = value.toArray();
    const auto inRange = [](const QJsonValue &side) { return side.isDouble() && side.toInt() >= 1 && side.toInt() <= QcPaintingMotif::maxSide; };
    return value.isArray() && tiles.size() == 2 && inRange(tiles[0]) && inRange(tiles[1]);
}

// Detail of a procedural texture above 16 x 16 (decision E-299): only the known fields, in range.
bool isDetail(const QJsonValue &value)
{
    const QJsonObject detail = value.toObject();
    const auto isShare = [](const QJsonValue &share) { return share.isDouble() && share.toDouble() >= 0.0 && share.toDouble() <= 1.0; };
    return value.isObject() && std::ranges::all_of(detail.keys(), [&detail, &isShare](const QString &key) {
               const QJsonValue field = detail.value(key);
               return ((key == u"grain"_s || key == u"bevel"_s) && isShare(field)) || (key == u"smooth"_s && field.isBool());
           });
}

} // namespace

namespace QcContentParser {

[[nodiscard]] QcResult<QcTextureDefinition> parseProceduralTexture(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"texture"_s, {}};
    QcTextureDefinition texture;
    texture.name = reader.name(u"name"_s, packNamespace);
    if (texture.name.isEmpty()) {
        return qcError(reader.ok() ? u"texture: 'name' is required"_s : reader.error);
    }

    if (!json.value(u"pattern"_s).isString()) {
        return qcError(u"texture '%1': 'pattern' string is required"_s.arg(texture.name));
    }

    // Only the fields the synthesizer reads are kept (short strings and numbers), so a texture
    // definition stays small whatever else the file contains.
    for (const QString &key : {u"pattern"_s, u"primary"_s, u"secondary"_s}) {
        const QJsonValue value = json.value(key);
        if (value.isString() && value.toString().size() <= 32) {
            texture.procedural.insert(key, value);
        } else if (!value.isUndefined()) {
            return qcError(u"texture '%1': '%2' must be a short string"_s.arg(texture.name, key));
        }
    }

    for (const QString &key : {u"variation"_s, u"seed"_s}) {
        const QJsonValue value = json.value(key);
        if (value.isDouble()) {
            texture.procedural.insert(key, value);
        } else if (!value.isUndefined()) {
            return qcError(u"texture '%1': '%2' must be a number"_s.arg(texture.name, key));
        }
    }

    if (json.contains(u"bitmap"_s) && !isBitmap(json.value(u"bitmap"_s))) {
        return qcError(u"texture '%1': 'bitmap' must be at most %2 strings of at most %2 characters"_s.arg(texture.name).arg(maxBitmapSide));
    }
    if (json.contains(u"palette"_s) && !isPalette(json.value(u"palette"_s))) {
        return qcError(u"texture '%1': 'palette' must map at most %2 single characters to colour strings"_s.arg(texture.name).arg(maxBitmapSide));
    }
    if (json.contains(u"detail"_s) && !isDetail(json.value(u"detail"_s))) {
        return qcError(u"texture '%1': 'detail' may only hold 'grain' and 'bevel' (0 to 1) and 'smooth' (true or false)"_s.arg(texture.name));
    }
    if (json.contains(u"tiles"_s) && !isTiles(json.value(u"tiles"_s))) {
        return qcError(u"texture '%1': 'tiles' must be [width, height] of 1 to %2"_s.arg(texture.name).arg(QcPaintingMotif::maxSide));
    }
    for (const QString &key : {u"bitmap"_s, u"palette"_s, u"detail"_s, u"tiles"_s}) {
        if (json.contains(key)) {
            texture.procedural.insert(key, json.value(key));
        }
    }

    return texture;
}

[[nodiscard]] QList<QcTextureDefinition> textureTiles(const QcTextureDefinition &texture)
{
    const QJsonArray tiles = texture.procedural.value(u"tiles"_s).toArray();
    const int width = tiles.size() == 2 ? tiles[0].toInt(1) : 1;
    const int height = tiles.size() == 2 ? tiles[1].toInt(1) : 1;
    if (width * height <= 1) {
        return {};
    }

    QList<QcTextureDefinition> parts;
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            QcTextureDefinition part = texture;
            part.name = QcPaintingMotif::tileName(texture.name, column, row);
            part.procedural.insert(u"tile"_s, QJsonArray{column, row});
            parts.append(part);
        }
    }
    return parts;
}

[[nodiscard]] QcResult<QHash<QString, QString>> parseTranslations(const QJsonObject &json)
{
    // Clients take at most this many per language (QcContentSerializer); a larger file is refused
    // before its table is built (decision E-371).
    if (std::cmp_greater(json.size(), QcStreamLimits::maxDefinitions)) {
        return qcError(u"more than %1 translations in one file"_s.arg(QcStreamLimits::maxDefinitions));
    }

    QHash<QString, QString> entries;
    for (auto it = json.constBegin(); it != json.constEnd(); ++it) {
        if (!it.value().isString()) {
            return qcError(u"translation '%1' must be a string"_s.arg(it.key()));
        }
        entries.insert(it.key(), it.value().toString());
    }
    return entries;
}

[[nodiscard]] QcResult<QcHandbookChapter> parseHandbookChapter(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"handbook chapter"_s, {}};
    QcHandbookChapter chapter;
    chapter.name = reader.name(u"name"_s, packNamespace);
    if (chapter.name.isEmpty()) {
        return qcError(reader.ok() ? u"handbook chapter: 'name' is required"_s : reader.error);
    }

    reader.context = u"handbook chapter '%1'"_s.arg(chapter.name);
    chapter.titleKey = reader.string(u"title"_s, QcContentFields::translationKey(u"chapter"_s, chapter.name));
    chapter.order = reader.integer(u"order"_s, chapter.order, -1000000, 1000000);
    chapter.iconItem = reader.name(u"icon"_s, packNamespace);

    static const QRegularExpression sectionPattern(u"^[a-z0-9_]{1,32}$"_s);
    const QJsonValue sections = json.value(u"sections"_s);
    if (!sections.isArray() || sections.toArray().isEmpty() || sections.toArray().size() > QcHandbookChapter::maxSections) {
        reader.fail(u"'sections' must be an array of 1..%1 section names"_s.arg(QcHandbookChapter::maxSections));
    }
    const QJsonArray sectionList = sections.toArray();
    for (const auto &section : sectionList) {
        if (!section.isString() || !sectionPattern.match(section.toString()).hasMatch() || chapter.sections.contains(section.toString())) {
            reader.fail(u"section names are distinct lower-case words"_s);
            break;
        }
        chapter.sections.append(section.toString());
    }

    const QJsonValue related = json.value(u"related"_s);
    if (!related.isUndefined() && (!related.isArray() || related.toArray().size() > QcHandbookChapter::maxRelated)) {
        reader.fail(u"'related' must be an array of at most %1 names"_s.arg(QcHandbookChapter::maxRelated));
    }
    const QJsonArray relatedList = related.toArray();
    for (const auto &name : relatedList) {
        const QString qualified = name.isString() ? QcContentNames::qualify(name.toString(), packNamespace) : QString();
        if (qualified.isEmpty()) {
            reader.fail(u"'related' entries must be content names"_s);
            break;
        }
        chapter.related.append(qualified);
    }

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return chapter;
}

} // namespace QcContentParser
