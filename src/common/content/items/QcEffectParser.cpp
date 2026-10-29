// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/items/QcEffectParser.h"

#include "common/content/registry/QcContentFields.h"
#include "common/content/registry/QcJsonFieldReader.h"

using namespace Qt::StringLiterals;

namespace QcEffectParser {

QcResult<QcEffectDefinition> parseEffect(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"effect"_s, {}};
    QcEffectDefinition effect;
    effect.name = reader.name(u"name"_s, packNamespace);
    if (effect.name.isEmpty()) {
        return qcError(reader.ok() ? u"effect: 'name' is required"_s : reader.error);
    }

    reader.context = u"effect '%1'"_s.arg(effect.name);
    effect.translationKey = reader.string(u"translation"_s, QcContentFields::translationKey(u"effect"_s, effect.name));
    effect.icon = reader.name(u"icon"_s, packNamespace);
    effect.colour = reader.colour(u"colour"_s).value_or(effect.colour);
    effect.tintsHearts = reader.boolean(u"tintsHearts"_s, effect.tintsHearts);
    effect.damage = reader.number(u"damage"_s, effect.damage, 0.0, 1000.0);
    effect.periodTicks = reader.integer(u"periodTicks"_s, effect.periodTicks, 1, 72000);
    effect.minHealth = reader.number(u"minHealth"_s, effect.minHealth, 0.0, 1000.0);
    effect.speed = reader.number(u"speed"_s, effect.speed, -1.0, 1.0);
    effect.exhaustion = reader.number(u"exhaustion"_s, effect.exhaustion, 0.0, 100.0);
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return effect;
}

} // namespace QcEffectParser
