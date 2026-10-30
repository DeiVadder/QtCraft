// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/generators/QcWorldOptions.h"

#include "common/content/registry/QcContentNames.h"
#include "worldgen/generators/QcWorldGenerators.h"

#include <algorithm>
#include <cmath>
#include <expected>

using namespace Qt::StringLiterals;

namespace {

using QcWorldOptions::Option;

// The base game's namespace: names on a command line without one are its.
constexpr auto baseNamespace = "qtcraft";

// Why `definition` cannot be used, or the usable option.
[[nodiscard]] std::expected<Option, QString> optionOf(const QcWorldOptionDefinition &definition, const QcContentRegistry &content)
{
    const QcWorldOptionTargets::Target *target = QcWorldOptionTargets::find(definition.target);
    if (!target) {
        return std::unexpected(u"unknown target '%1'"_s.arg(definition.target));
    }
    if (target->needsSubject == definition.subject.isEmpty()) {
        return std::unexpected(target->needsSubject ? u"target '%1' needs a 'subject'"_s.arg(definition.target)
                                                    : u"target '%1' takes no 'subject'"_s.arg(definition.target));
    }
    if (!std::ranges::contains(content.worldgen().worldOptionGroups(), definition.group, &QcWorldOptionGroup::name)) {
        return std::unexpected(u"unknown group '%1'"_s.arg(definition.group));
    }
    const std::optional<double> current = target->read(content, definition.subject);
    if (!current) {
        return std::unexpected(u"unknown subject '%1'"_s.arg(definition.subject));
    }

    const bool isSwitch = target->kind == QcWorldOptionTargets::Kind::Switch;
    const double low = isSwitch ? 0.0 : definition.min;
    const double high = isSwitch ? 1.0 : definition.max;
    if (low < target->low || high > target->high) {
        return std::unexpected(u"range %1..%2 is outside the target's %3..%4"_s.arg(low).arg(high).arg(target->low).arg(target->high));
    }
    const double fallback = definition.defaultValue.value_or(*current);
    if (fallback < low || fallback > high || (isSwitch && fallback != 0.0 && fallback != 1.0)) {
        return std::unexpected(u"default %1 is outside %2..%3"_s.arg(fallback).arg(low).arg(high));
    }
    return Option{&definition, target, fallback};
}

// The usable option named `name`; null when there is none.
[[nodiscard]] const Option *findOption(const QList<Option> &options, const QString &name)
{
    const auto it = std::ranges::find(options, name, [](const Option &option) { return option.definition->name; });
    return it == options.end() ? nullptr : &*it;
}

// A value as written on a command line: a number, or on/off, true/false.
[[nodiscard]] std::optional<QJsonValue> textValue(const QString &text, const Option &option)
{
    if (option.isSwitch()) {
        if (text == u"on"_s || text == u"true"_s) {
            return QJsonValue(true);
        }
        if (text == u"off"_s || text == u"false"_s) {
            return QJsonValue(false);
        }
        return std::nullopt;
    }

    bool ok = false;
    const double number = text.toDouble(&ok);
    return ok ? std::optional<QJsonValue>(number) : std::nullopt;
}

// The values the preset `name` names (only those, of options that exist).
[[nodiscard]] QcResult<QJsonObject> namedValues(const QString &name, const QList<Option> &options, const QcContentRegistry &content)
{
    const QcWorldPreset *preset = content.worldgen().worldPreset(QcContentNames::qualify(name, QString::fromLatin1(baseNamespace)));
    if (!preset) {
        return qcError(u"Unknown world preset '%1'"_s.arg(name));
    }

    QJsonObject values;
    for (auto it = preset->values.begin(); it != preset->values.end(); ++it) {
        if (const Option *option = findOption(options, it.key())) {
            values.insert(it.key(), option->json(it.value()));
        }
    }
    return values;
}

// 10^d for the fewest decimals d (at most 9) that write both `step` and `min` exactly.
[[nodiscard]] double decimalScale(double step, double min)
{
    double scale = 1.0;
    for (int decimals = 0; decimals < 9; ++decimals) {
        const auto whole = [scale](double number) { return std::abs(number * scale - std::round(number * scale)) < 1e-9 * scale; };
        if (whole(step) && whole(min)) {
            break;
        }
        scale *= 10.0;
    }
    return scale;
}

} // namespace

namespace QcWorldOptions {

QJsonValue Option::json(double value) const
{
    return isSwitch() ? QJsonValue(value != 0.0) : QJsonValue(value);
}

std::optional<double> Option::read(const QJsonValue &json, bool strict) const
{
    if (isSwitch()) {
        return json.isBool() ? std::optional<double>(json.toBool() ? 1.0 : 0.0) : std::nullopt;
    }
    if (!json.isDouble() || !std::isfinite(json.toDouble())) {
        return std::nullopt;
    }

    const double value = json.toDouble();
    const double low = strict ? definition->min : target->low;
    const double high = strict ? definition->max : target->high;
    return value >= low && value <= high ? std::optional<double>(value) : std::nullopt;
}

double Option::snapped(double value) const
{
    if (isSwitch() || !std::isfinite(value)) {
        return value;
    }

    const double step = definition->step;
    double snapped = value;
    if (step > 0.0) {
        snapped = definition->min + std::round((value - definition->min) / step) * step;
        snapped = std::round(snapped * decimalScale(step, definition->min)) / decimalScale(step, definition->min);
    }

    // Within rounding of the default: the default itself, so that it compares equal.
    const double tolerance = 1e-9 * std::max({1.0, std::abs(step), std::abs(defaultValue)});
    return std::abs(snapped - defaultValue) <= tolerance ? defaultValue : snapped;
}

bool takesOptions(const QString &type)
{
    return type == QLatin1StringView(QcWorldGenerators::terrain);
}

QList<Option> optionsOf(const QcContentRegistry &content)
{
    QList<Option> options;
    for (const QcWorldOptionDefinition &definition : content.worldgen().worldOptions()) {
        if (const std::expected<Option, QString> option = optionOf(definition, content)) {
            options.append(*option);
        }
    }
    return options;
}

QStringList check(const QcContentRegistry &content)
{
    QStringList warnings;
    for (const QcWorldOptionDefinition &definition : content.worldgen().worldOptions()) {
        if (const std::expected<Option, QString> option = optionOf(definition, content); !option) {
            warnings.append(u"world option %1: %2; left out"_s.arg(definition.name, option.error()));
        }
    }

    const QList<Option> options = optionsOf(content);
    for (const QcWorldPreset &preset : content.worldgen().worldPresets()) {
        for (auto it = preset.values.begin(); it != preset.values.end(); ++it) {
            const Option *option = findOption(options, it.key());
            if (!option || !option->read(option->json(it.value()), true)) {
                warnings.append(u"world preset %1: no option %2 with the value %3; left out"_s.arg(preset.name, it.key()).arg(it.value()));
            }
        }
    }
    return warnings;
}

QJsonObject defaults(const QcContentRegistry &content)
{
    QJsonObject values;
    const QList<Option> options = optionsOf(content);
    for (const Option &option : options) {
        values.insert(option.definition->name, option.json(option.defaultValue));
    }
    return values;
}

QcResult<QJsonObject> valuesToStore(const QString &type, const QJsonObject &given, const QcContentRegistry &content)
{
    if (!takesOptions(type)) {
        return given.isEmpty() ? QcResult<QJsonObject>(QJsonObject{}) : qcError(u"The world type '%1' takes no world options"_s.arg(type));
    }

    const QList<Option> options = optionsOf(content);
    QJsonObject values = defaults(content);
    for (auto it = given.begin(); it != given.end(); ++it) {
        const Option *option = findOption(options, it.key());
        if (!option) {
            return qcError(u"Unknown world option '%1'"_s.arg(it.key()));
        }
        const std::optional<double> value = option->read(it.value(), true);
        if (!value) {
            return qcError(option->isSwitch() ? u"The world option '%1' is on or off"_s.arg(it.key())
                                              : u"The world option '%1' takes a number within %2..%3"_s.arg(it.key())
                                                    .arg(option->definition->min)
                                                    .arg(option->definition->max));
        }
        values.insert(it.key(), option->json(*value));
    }
    return values;
}

QcResult<QJsonObject> presetValues(const QString &name, const QcContentRegistry &content)
{
    const QcWorldPreset *preset = content.worldgen().worldPreset(QcContentNames::qualify(name, QString::fromLatin1(baseNamespace)));
    if (!preset) {
        return qcError(u"Unknown world preset '%1'"_s.arg(name));
    }

    const QList<Option> options = optionsOf(content);
    QJsonObject values = defaults(content);
    for (auto it = preset->values.begin(); it != preset->values.end(); ++it) {
        // A value that does not fit is left out (check() warns about it).
        if (const Option *option = findOption(options, it.key()); option && option->read(option->json(it.value()), true)) {
            values.insert(it.key(), option->json(it.value()));
        }
    }
    return values;
}

QString presetOf(const QJsonObject &values, const QcContentRegistry &content)
{
    for (const QcWorldPreset &preset : content.worldgen().worldPresets()) {
        if (const QcResult<QJsonObject> presetted = presetValues(preset.name, content); presetted && *presetted == values) {
            return preset.name;
        }
    }
    return {};
}

QcResult<QJsonObject> parseText(const QString &text, const QcContentRegistry &content)
{
    const QList<Option> options = optionsOf(content);
    const QString base = QString::fromLatin1(baseNamespace);
    const QStringList entries = text.split(u',', Qt::SkipEmptyParts);
    const auto keyOf = [](const QString &entry) { return entry.left(entry.indexOf(u'=')).trimmed(); };
    const auto valueOf = [](const QString &entry) {
        const qsizetype equals = entry.indexOf(u'=');
        return equals < 0 ? QString() : entry.mid(equals + 1).trimmed();
    };

    // Presets first, so that a value named explicitly wins wherever it stands.
    QJsonObject values;
    for (const QString &entry : entries) {
        if (keyOf(entry) != u"preset"_s) {
            continue;
        }
        const QcResult<QJsonObject> preset = namedValues(valueOf(entry), options, content);
        if (!preset) {
            return qcError(preset.error());
        }
        for (auto it = preset->begin(); it != preset->end(); ++it) {
            values.insert(it.key(), it.value());
        }
    }

    for (const QString &entry : entries) {
        const QString key = keyOf(entry);
        if (key == u"preset"_s) {
            continue;
        }
        const QString name = QcContentNames::qualify(key, base);
        const Option *option = findOption(options, name);
        const QString value = valueOf(entry);
        const std::optional<QJsonValue> json = option ? textValue(value, *option) : std::nullopt;
        if (!json) {
            return qcError(option ? u"Invalid value '%1' of the world option '%2'"_s.arg(value, key) : u"Unknown world option '%1'"_s.arg(key));
        }
        values.insert(name, *json);
    }
    return values;
}

Applied apply(std::shared_ptr<const QcContentRegistry> content, const QString &type, const QJsonObject &values)
{
    if (values.isEmpty() || !takesOptions(type)) {
        return {std::move(content), {}};
    }

    const QList<Option> options = optionsOf(*content);
    auto applied = std::make_shared<QcContentRegistry>(*content);
    QStringList warnings;
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (!findOption(options, it.key())) {
            warnings.append(u"The world option '%1' is not known to the loaded content (a mod missing?); it is ignored"_s.arg(it.key()));
        }
    }
    // In the content's order, so that two options on one target always meet the same way.
    for (const Option &option : options) {
        const QJsonValue json = values.value(option.definition->name);
        if (json.isUndefined()) {
            continue;
        }
        if (const std::optional<double> value = option.read(json, false)) {
            option.target->write(*applied, option.definition->subject, *value);
        } else {
            warnings.append(u"The world option '%1' has the invalid value %2; the content's value is used"_s.arg(option.definition->name,
                                                                                                                 json.toVariant().toString()));
        }
    }
    return {std::move(applied), warnings};
}

} // namespace QcWorldOptions
