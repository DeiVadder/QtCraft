// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/items/QcEffectParser.h"

#include "common/content/registry/QcContentFields.h"
#include "common/content/registry/QcJsonFieldReader.h"

#include <utility>

using namespace Qt::StringLiterals;

namespace {

// Returns `effect` with the fields of what it does to a body besides damage and speed read
// (decision E-992).
QcEffectDefinition withBodyFields(QcJsonFieldReader &reader, QcEffectDefinition effect)
{
    effect.heal = reader.number(u"heal"_s, effect.heal, 0.0, 1000.0);
    effect.instant = reader.boolean(u"instant"_s, effect.instant);
    effect.reversedOnUndead = reader.boolean(u"reversedOnUndead"_s, effect.reversedOnUndead);
    effect.undeadAmount = reader.number(u"undeadAmount"_s, effect.undeadAmount, 0.0, 1000.0);
    effect.attack = reader.number(u"attack"_s, effect.attack, -1000.0, 1000.0);
    effect.resistance = reader.number(u"resistance"_s, effect.resistance, 0.0, 1.0);
    effect.fireProof = reader.boolean(u"fireProof"_s, effect.fireProof);
    effect.waterBreathing = reader.boolean(u"waterBreathing"_s, effect.waterBreathing);
    effect.nightVision = reader.boolean(u"nightVision"_s, effect.nightVision);
    effect.visibility = reader.number(u"visibility"_s, effect.visibility, 0.0, 1.0);
    effect.jump = reader.number(u"jump"_s, effect.jump, 0.0, 2.0);
    effect.maxFallSpeed = reader.number(u"maxFallSpeed"_s, effect.maxFallSpeed, 0.0, QcEffectDefinition::fastestFallLimit);
    effect.mining = reader.number(u"mining"_s, effect.mining, -1.0, 10.0);
    effect.absorption = reader.number(u"absorption"_s, effect.absorption, 0.0, 1000.0);
    return effect;
}

} // namespace

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
    effect.food = reader.integer(u"food"_s, effect.food, 0, QcEffectDefinition::maxFoodPerAct);
    effect.saturation = reader.number(u"saturation"_s, effect.saturation, 0.0, QcEffectDefinition::maxFoodPerAct);
    QcEffectDefinition read = withBodyFields(reader, std::move(effect));
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return read;
}

} // namespace QcEffectParser
