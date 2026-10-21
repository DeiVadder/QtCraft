#include "common/content/parser/QcHoldParser.h"

#include "common/content/parser/QcJsonFieldReader.h"
#include "common/core/QcLogging.h"

#include <QJsonArray>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// Warns about the keys of `json` that are not in `known`.
void warnUnknownKeys(const QJsonObject &json, const QStringList &known, const QString &context)
{
    const QStringList keys = json.keys();
    for (const QString &key : keys) {
        if (!known.contains(key)) {
            qCWarning(lcRegistry).noquote() << context << u"has an unknown key '%1', ignored"_s.arg(key);
        }
    }
}

// The bounds one value of a vector must keep.
struct Range
{
    double low;
    double high;
};

// [x, y, z] at `key` with every value within `range`; a single number stands for all three when
// `uniform` allows it.
std::optional<QcVec3> vectorAt(QcJsonFieldReader &reader, const QString &key, Range range, bool uniform)
{
    const QJsonValue value = reader.json.value(key);
    if (value.isUndefined()) {
        return std::nullopt;
    }

    QcVec3 vector;
    if (uniform && value.isDouble()) {
        vector = {value.toDouble(), value.toDouble(), value.toDouble()};
    } else if (value.isArray() && value.toArray().size() == 3) {
        const QJsonArray array = value.toArray();
        vector = {array[0].toDouble(NAN), array[1].toDouble(NAN), array[2].toDouble(NAN)};
    } else {
        reader.fail(uniform ? u"'%1' must be a number or [x, y, z]"_s.arg(key) : u"'%1' must be [x, y, z]"_s.arg(key));
        return std::nullopt;
    }

    for (const double v : {vector.x, vector.y, vector.z}) {
        if (!(v >= range.low && v <= range.high)) {
            reader.fail(u"'%1' needs numbers from %2 to %3"_s.arg(key).arg(range.low).arg(range.high));
            return std::nullopt;
        }
    }
    return vector;
}

// A placement; "scale" only where `withScale` (not in "swing", which keeps the item's size).
QcHoldTransform transformOf(const QJsonObject &json, QcJsonFieldReader &reader, bool withScale)
{
    QStringList known{u"translate"_s, u"rotate"_s};
    if (withScale) {
        known.append(u"scale"_s);
    }
    warnUnknownKeys(json, known, reader.context);

    QcHoldTransform transform;
    const double move = QcHoldParser::maxTranslate;
    transform.translate = vectorAt(reader, u"translate"_s, {-move, move}, false);
    transform.rotate = vectorAt(reader, u"rotate"_s, {-QcHoldParser::maxRotate, QcHoldParser::maxRotate}, false);
    if (withScale) {
        transform.scale = vectorAt(reader, u"scale"_s, {QcHoldParser::minScale, QcHoldParser::maxScale}, true);
    }
    return transform;
}

// A hold object; "category" only where `withCategory`.
QcHoldDisplay displayOf(const QJsonObject &json, QcJsonFieldReader &reader, bool withCategory)
{
    QStringList known{u"grip"_s, u"firstPerson"_s, u"thirdPerson"_s, u"swing"_s};
    if (withCategory) {
        known.append(u"category"_s);
    }
    warnUnknownKeys(json, known, reader.context);

    QcHoldDisplay display;
    const QString grip = reader.string(u"grip"_s);
    if (!grip.isEmpty()) {
        display.grip = QcHoldNames::grip(grip);
        if (!display.grip) {
            reader.fail(u"'grip' must be \"base\", \"handle\" or \"middle\""_s);
        }
    }
    const QString category = withCategory ? reader.string(u"category"_s) : QString();
    if (!category.isEmpty()) {
        display.category = QcHoldNames::category(category);
        if (!display.category) {
            reader.fail(u"unknown 'category' \"%1\""_s.arg(category));
        }
    }
    reader.nestedObject(u"firstPerson"_s, [&display](const QJsonObject &object, QcJsonFieldReader &nested) {
        display.firstPerson = transformOf(object, nested, true);
    });
    reader.nestedObject(u"thirdPerson"_s, [&display](const QJsonObject &object, QcJsonFieldReader &nested) {
        display.thirdPerson = transformOf(object, nested, true);
    });
    reader.nestedObject(u"swing"_s, [&display](const QJsonObject &object, QcJsonFieldReader &nested) {
        display.swing = transformOf(object, nested, false);
    });
    return display;
}

} // namespace

namespace QcHoldParser {

QcHoldDisplay parseHold(const QJsonObject &json, QcJsonFieldReader &reader)
{
    return displayOf(json, reader, true);
}

QcHoldDisplay holdOf(QcJsonFieldReader &reader)
{
    QcHoldDisplay display;
    reader.nestedObject(u"hold"_s, [&display](const QJsonObject &object, QcJsonFieldReader &nested) { display = parseHold(object, nested); });
    return display;
}

// The namespace is unused: categories are fixed names, not ids of the pack. It stays in the
// signature every settings file parser has (PackReader::readSettingsFile calls them alike).
QcResult<QcHoldDefaults> parseHoldDefaults(const QJsonObject &json, const QString &)
{
    QcJsonFieldReader reader{json, u"held"_s, {}};
    warnUnknownKeys(json, {u"categories"_s}, reader.context);
    QcHoldDefaults defaults;
    reader.nestedObject(u"categories"_s, [&defaults](const QJsonObject &object, QcJsonFieldReader &categories) {
        const QStringList keys = object.keys();
        for (const QString &key : keys) {
            const std::optional<QcHoldCategory> category = QcHoldNames::category(key);
            if (!category) {
                qCWarning(lcRegistry).noquote() << categories.context << u"has an unknown category '%1', ignored"_s.arg(key);
                continue;
            }
            categories.nestedObject(key, [&](const QJsonObject &entry, QcJsonFieldReader &nested) {
                defaults.categories[std::size_t(*category)] = displayOf(entry, nested, false);
            });
        }
    });
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return defaults;
}

} // namespace QcHoldParser
