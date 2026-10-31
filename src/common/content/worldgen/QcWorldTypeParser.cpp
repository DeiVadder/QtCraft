// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/worldgen/QcWorldTypeParser.h"

#include "common/content/registry/QcContentNames.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/content/registry/QcJsonFieldReader.h"
#include "common/core/QcWorldConstants.h"
#include "common/world/QcDayCycle.h"

using namespace Qt::StringLiterals;

namespace {

// "timeOfDay" (seconds after sunrise; the time then stands still unless "daylightCycle" says
// otherwise), "daylightCycle" and "mobSpawning".
QcWorldTypeRules parseRules(const QJsonObject &json, QcJsonFieldReader &reader, QcWorldTypeRules rules)
{
    if (json.contains(u"timeOfDay"_s)) {
        rules.timeOfDay = reader.number(u"timeOfDay"_s, 0.0, 0.0, QcDayCycle::dayLength);
        rules.daylightCycle = false;
    }
    rules.daylightCycle = reader.boolean(u"daylightCycle"_s, rules.daylightCycle);
    rules.mobSpawning = reader.boolean(u"mobSpawning"_s, rules.mobSpawning);
    return rules;
}

// One entry of a layer list: the count part with its '*' ("59*", empty without) and the block name.
struct PresetEntry
{
    QString count;
    QString name;
};

// The entries of the layer list `preset` ("[count*]block", QcFlatGenerator::parseLayers), trimmed.
[[nodiscard]] QList<PresetEntry> entriesOf(const QString &preset)
{
    QList<PresetEntry> entries;
    const QStringList raws = preset.split(u',', Qt::SkipEmptyParts);
    for (const QString &raw : raws) {
        const QString entry = raw.trimmed();
        const qsizetype star = entry.indexOf(u'*');
        entries.append({entry.left(star + 1), entry.mid(star + 1).trimmed()});
    }
    return entries;
}

// The layer list `preset` with every block name qualified by `packNamespace`; std::nullopt when a
// name is invalid.
[[nodiscard]] std::optional<QString> qualifiedPreset(const QString &preset, const QString &packNamespace)
{
    QStringList entries;
    const QList<PresetEntry> parsed = entriesOf(preset);
    for (const PresetEntry &entry : parsed) {
        const QString name = QcContentNames::qualify(entry.name, packNamespace);
        if (name.isEmpty()) {
            return std::nullopt;
        }
        entries.append(entry.count + name);
    }
    return entries.join(u',');
}

} // namespace

namespace QcWorldTypeParser {

[[nodiscard]] QcResult<QcFlatDefinition> parseFlat(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"flat world"_s, {}};
    QcFlatDefinition flat;
    const QString preset = reader.string(u"preset"_s, flat.preset);
    flat.rules = parseRules(json, reader, flat.rules);
    // Block names without a namespace are the pack's, as everywhere in its data.
    const std::optional<QString> qualified = qualifiedPreset(preset, packNamespace);
    if (preset.isEmpty() || preset.size() > 4096 || !qualified || qualified->size() > 4096) {
        reader.fail(u"'preset' must be a layer list of at most 4096 characters with valid block names"_s);
    }
    flat.preset = qualified.value_or(flat.preset);
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return flat;
}

[[nodiscard]] QcResult<QcVoidDefinition> parseVoid(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"void world"_s, {}};
    QcVoidDefinition empty;
    empty.platform = reader.name(u"platform"_s, packNamespace, empty.platform);
    empty.platformRadius = reader.integer(u"platformRadius"_s, empty.platformRadius, 0, 16);
    empty.platformY = reader.integer(u"platformY"_s, empty.platformY, 1, QcWorld::worldHeight - 4);
    empty.rules = parseRules(json, reader, empty.rules);
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return empty;
}

QString unknownPresetBlock(const QString &preset, const QcContentRegistry &content)
{
    const QList<PresetEntry> entries = entriesOf(preset);
    for (const PresetEntry &entry : entries) {
        const QString name = QcContentNames::qualify(entry.name, u"qtcraft"_s);
        if (!name.isEmpty() && !content.blockId(name)) {
            return name;
        }
    }
    return {};
}

QString shortPreset(const QString &preset)
{
    static constexpr QLatin1StringView basePrefix{"qtcraft:"};
    QStringList entries;
    const QList<PresetEntry> parsed = entriesOf(preset);
    for (const PresetEntry &entry : parsed) {
        entries.append(entry.count + (entry.name.startsWith(basePrefix) ? entry.name.mid(basePrefix.size()) : entry.name));
    }
    return entries.join(u',');
}

} // namespace QcWorldTypeParser
