// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/worldgen/QcWorldTypeParser.h"

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

} // namespace

namespace QcWorldTypeParser {

[[nodiscard]] QcResult<QcFlatDefinition> parseFlat(const QJsonObject &json, const QString &packNamespace)
{
    Q_UNUSED(packNamespace);
    QcJsonFieldReader reader{json, u"flat world"_s, {}};
    QcFlatDefinition flat;
    flat.preset = reader.string(u"preset"_s, flat.preset);
    flat.rules = parseRules(json, reader, flat.rules);
    if (flat.preset.isEmpty() || flat.preset.size() > 4096) {
        reader.fail(u"'preset' must be a layer list of at most 4096 characters"_s);
    }
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

} // namespace QcWorldTypeParser
