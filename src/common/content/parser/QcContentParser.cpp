#include "QcContentParser.h"

#include "QcContentFields.h"
#include "QcContentNames.h"
#include "QcJsonFieldReader.h"

#include <QJsonArray>
#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace QcContentParser {

QcResult<QcTextureDefinition> parseProceduralTexture(const QJsonObject &json, const QString &packNamespace)
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

QcResult<QcHandbookChapter> parseHandbookChapter(const QJsonObject &json, const QString &packNamespace)
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
