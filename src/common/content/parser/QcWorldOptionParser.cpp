#include "common/content/parser/QcWorldOptionParser.h"

#include "common/content/parser/QcContentFields.h"
#include "common/content/parser/QcJsonFieldReader.h"
#include "common/content/registry/QcContentNames.h"

#include <QJsonArray>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// Numbers of options stay within this size; the targets narrow them further.
constexpr double largest = 1.0e6;

// A value of an option in JSON: a number, or true/false for a switch (1/0).
[[nodiscard]] std::optional<double> valueOf(const QJsonValue &value)
{
    if (value.isBool()) {
        return value.toBool() ? 1.0 : 0.0;
    }
    if (value.isDouble() && std::isfinite(value.toDouble()) && std::abs(value.toDouble()) <= largest) {
        return value.toDouble();
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<QcWorldOptionUnit> unitOf(const QString &name)
{
    if (name.isEmpty() || name == u"plain"_s) {
        return QcWorldOptionUnit::Plain;
    }
    if (name == u"blocks"_s) {
        return QcWorldOptionUnit::Blocks;
    }
    if (name == u"percent"_s) {
        return QcWorldOptionUnit::Percent;
    }
    if (name == u"factor"_s) {
        return QcWorldOptionUnit::Factor;
    }
    return std::nullopt;
}

// The objects of the array at `key` (missing: none).
[[nodiscard]] QList<QJsonObject> objectsAt(const QJsonObject &json, const QString &key, QcJsonFieldReader &reader)
{
    const QJsonValue value = json.value(key);
    if (value.isUndefined()) {
        return {};
    }
    if (!value.isArray() || value.toArray().size() > QcWorldOptionParser::maxEntries) {
        reader.fail(u"'%1' must be an array of at most %2 objects"_s.arg(key).arg(QcWorldOptionParser::maxEntries));
        return {};
    }

    QList<QJsonObject> objects;
    const QJsonArray entries = value.toArray();
    for (const auto &entry : entries) {
        if (!entry.isObject()) {
            reader.fail(u"'%1' must hold objects"_s.arg(key));
            return {};
        }
        objects.append(entry.toObject());
    }
    return objects;
}

// The required name of an entry, qualified with the pack's namespace.
QString requiredName(const QString &key, const QString &packNamespace, QcJsonFieldReader &reader)
{
    const QString name = reader.name(key, packNamespace);
    if (name.isEmpty()) {
        reader.fail(u"'%1' is missing"_s.arg(key));
    }
    return name;
}

QcWorldOptionDefinition parseOption(const QJsonObject &json, const QString &packNamespace, QcJsonFieldReader &parent)
{
    QcJsonFieldReader reader{json, parent.context + u" option"_s, {}};
    QcWorldOptionDefinition option;
    option.name = requiredName(u"name"_s, packNamespace, reader);
    option.translationKey = QcContentFields::translationKey(u"world_option"_s, option.name);
    option.group = requiredName(u"group"_s, packNamespace, reader);
    option.target = reader.string(u"target"_s);
    option.subject = reader.name(u"subject"_s, packNamespace);
    option.min = reader.number(u"min"_s, option.min, -largest, largest);
    option.max = reader.number(u"max"_s, option.max, -largest, largest);
    option.step = reader.number(u"step"_s, option.step, 0.0, largest);

    if (const std::optional<QcWorldOptionUnit> unit = unitOf(reader.string(u"unit"_s))) {
        option.unit = *unit;
    } else {
        reader.fail(u"'unit' must be plain, blocks, percent or factor"_s);
    }
    if (option.target.isEmpty()) {
        reader.fail(u"'target' is missing"_s);
    }
    if (option.min > option.max) {
        reader.fail(u"'min' is above 'max'"_s);
    }
    if (json.contains(u"default"_s)) {
        option.defaultValue = valueOf(json.value(u"default"_s));
        if (!option.defaultValue) {
            reader.fail(u"'default' must be a number or true/false"_s);
        }
    }
    parent.adopt(reader);
    return option;
}

QcWorldPreset parsePreset(const QJsonObject &json, const QString &packNamespace, QcJsonFieldReader &parent)
{
    QcJsonFieldReader reader{json, parent.context + u" preset"_s, {}};
    QcWorldPreset preset;
    preset.name = requiredName(u"name"_s, packNamespace, reader);
    preset.translationKey = QcContentFields::translationKey(u"world_preset"_s, preset.name);

    const QJsonObject values = json.value(u"values"_s).toObject();
    if (values.size() > QcWorldOptionParser::maxEntries) {
        reader.fail(u"'values' holds more than %1 options"_s.arg(QcWorldOptionParser::maxEntries));
    }
    for (auto it = values.begin(); it != values.end() && reader.ok(); ++it) {
        const QString option = QcContentNames::qualify(it.key(), packNamespace);
        const std::optional<double> value = valueOf(it.value());
        if (option.isEmpty() || !value) {
            reader.fail(u"'values' needs option names with a number or true/false ('%1')"_s.arg(it.key()));
            continue;
        }
        preset.values.insert(option, *value);
    }
    parent.adopt(reader);
    return preset;
}

} // namespace

namespace QcWorldOptionParser {

[[nodiscard]] QcResult<QcWorldOptionSet> parseOptionSet(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"world options"_s, {}};
    QcWorldOptionSet set;
    const QList<QJsonObject> groups = objectsAt(json, u"groups"_s, reader);
    const QList<QJsonObject> options = objectsAt(json, u"options"_s, reader);
    const QList<QJsonObject> presets = objectsAt(json, u"presets"_s, reader);
    for (const QJsonObject &entry : groups) {
        QcJsonFieldReader nested{entry, u"world options group"_s, {}};
        QcWorldOptionGroup group;
        group.name = requiredName(u"name"_s, packNamespace, nested);
        group.translationKey = QcContentFields::translationKey(u"world_option_group"_s, group.name);
        reader.adopt(nested);
        set.groups.append(group);
    }
    for (const QJsonObject &entry : options) {
        set.options.append(parseOption(entry, packNamespace, reader));
    }
    for (const QJsonObject &entry : presets) {
        set.presets.append(parsePreset(entry, packNamespace, reader));
    }

    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return set;
}

} // namespace QcWorldOptionParser
