// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/gameplay/QcServerEffects.h"

#include "common/gameplay/QcEffectRules.h"
#include "common/physics/QcPhysics.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcPlayerHealth.h"
#include "server/gameplay/QcServerCombat.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace {

// `effect` as a body gets it: an instant kind lasts the one tick in which it acts.
QcStatusEffect asGiven(const QcEffectDefinition &kind, QcStatusEffect effect)
{
    effect.ticksLeft = kind.instant ? 1 : effect.ticksLeft;
    return effect;
}

} // namespace

QcServerEffects::QcServerEffects(QcServerState &state, QcServerSounds &sounds, QcPlayerHealth &health)
    : m_state(state)
    , m_sounds(sounds)
    , m_health(health)
{
}

void QcServerEffects::tick()
{
    for (const auto &client : m_state.clients()) {
        if (client->state == QcServerClient::State::Playing) {
            tickPlayer(*client);
        }
    }
    tickCreatures();
}

bool QcServerEffects::giveCreature(const QcContentRegistry &content, QcMobManager &creatures, quint32 creature, const QcStatusEffect &effect)
{
    QcCreature *mob = creatures.find(creature);
    const QcEffectDefinition *kind = content.effect(effect.name);
    if (!mob || !kind || content.mobs()[mob->type].immuneTo.contains(effect.name)) {
        return false;
    }
    if (!mob->effects) {
        mob->effects.emplace();
    }
    return mob->effects->give(asGiven(*kind, effect));
}

bool QcServerEffects::givePlayer(const QcContentRegistry &content, QcServerClient &client, const QcStatusEffect &effect)
{
    const QcEffectDefinition *kind = content.effect(effect.name);
    if (client.dead || !kind || !client.effects.give(asGiven(*kind, effect))) {
        return false;
    }
    client.absorption = std::max(client.absorption, QcEffectRules::absorptionLimit(content, client.effects.all()));
    return true;
}

bool QcServerEffects::givePlayer(QcServerState &state, QcServerClient &client, const QcStatusEffect &effect)
{
    if (!givePlayer(state.content(), client, effect)) {
        return false;
    }
    state.advancements().trigger(client, {.trigger = QcAdvancementTrigger::EffectGained, .effect = effect.name});
    return true;
}

void QcServerEffects::tickPlayer(QcServerClient &client)
{
    if (client.dead) {
        client.effects.clear();
    }

    // What each effect does in this tick, before it counts down (the reference hurts at the start).
    const QList<QcStatusEffect> effects = client.effects.all();
    for (const QcStatusEffect &effect : effects) {
        if (const QcEffectDefinition *definition = m_state.content().effect(effect.name)) {
            actOnPlayer(client, *definition, effect);
        }
    }
    settleAbsorption(client);
    const bool ended = client.effects.tick();
    client.sentEffects.tick();

    // Every change at once (the sent copy counts down alike, so the countdown alone is none; an
    // ended effect is one), and the time left every second while any lasts.
    const bool changed = ended || client.sentEffects != client.effects;
    const bool due = !client.effects.isEmpty() && m_state.clock().tick() % sendIntervalTicks == 0;
    if (changed || due) {
        m_health.sendHealth(client);
    }
}

void QcServerEffects::actOnPlayer(QcServerClient &client, const QcEffectDefinition &definition, const QcStatusEffect &effect)
{
    const double damage = definition.hurtsAt(effect.level, effect.ticksLeft) ? definition.damageTo(client.health, effect.level, effect.strength) : 0.0;
    if (damage > 0.0) {
        m_health.damage(client, damage, {}, QcPlayerHealth::DamageKind::Effect);
    }
    if (client.dead) {
        return;
    }
    if (definition.healsAt(effect.level, effect.ticksLeft) && client.health < QcServerClient::maxHealth) {
        client.health = std::min(client.health + definition.amountAt(definition.heal, effect.level, effect.strength), QcServerClient::maxHealth);
        m_health.sendHealth(client);
    }
    // Hunger (decision E-838): food exhaustion every tick, times the level.
    if (definition.exhaustion > 0.0) {
        client.hunger.exhaust(definition.exhaustion * effect.level * QcPhysics::tickDuration);
    }
    // Saturation (decision E-1001): food and saturation each time it acts, times the level; the
    // tick sends the changed food.
    if ((definition.food > 0 || definition.saturation > 0.0) && definition.actsAt(effect.level, effect.ticksLeft)) {
        client.hunger.feed(definition.food * effect.level, definition.saturation * effect.level);
    }
}

void QcServerEffects::settleAbsorption(QcServerClient &client) const
{
    const QcContentRegistry &content = m_state.content();
    const double limit = QcEffectRules::absorptionLimit(content, client.effects.all());
    client.absorption = std::min(client.absorption, limit);
    if (limit <= 0.0 || client.absorption > 0.0) {
        return;
    }
    const QList<QcStatusEffect> effects = client.effects.all();
    for (const QcStatusEffect &effect : effects) {
        const QcEffectDefinition *definition = content.effect(effect.name);
        if (definition && definition->absorption > 0.0) {
            client.effects.remove(effect.name);
        }
    }
}

QcServerEffects::CreatureHurt QcServerEffects::tickCreature(QcCreature &mob)
{
    const QcMobDefinition &kind = m_state.content().mobs()[mob.type];
    CreatureHurt result{.creature = mob.id};
    for (const QcStatusEffect &effect : mob.effects->all()) {
        const QcEffectDefinition *definition = m_state.content().effect(effect.name);
        if (!definition) {
            continue;
        }
        const bool hurts = definition->hurtsAt(effect.level, effect.ticksLeft);
        const double hurt = hurts ? definition->damageTo(mob.health, effect.level, effect.strength) : 0.0;
        const double heal = definition->healsAt(effect.level, effect.ticksLeft) ? definition->amountAt(definition->heal, effect.level, effect.strength) : 0.0;
        // Instant health hurts the undead, instant damage heals them, each by the effect's amount
        // for the undead.
        const bool reversed = definition->reversedOnUndead && kind.undead;
        const double undead = definition->amountAt(definition->undeadAmount, effect.level, effect.strength);
        const double dealt = reversed ? (heal > 0.0 ? undead : 0.0) : hurt;
        result.damage += dealt;
        result.attacker = dealt > 0.0 && effect.giver != 0 ? effect.giver : result.attacker;
        mob.health = std::min(mob.health + (reversed ? (hurts ? undead : 0.0) : heal), kind.maxHealth);
    }

    mob.effects->tick();
    if (mob.effects->isEmpty()) {
        mob.effects.reset();
    }
    return result;
}

void QcServerEffects::tickCreatures()
{
    // Hurting may kill a creature (an effect of a mod without minHealth) and so change the list.
    QList<CreatureHurt> hurts;
    for (QcCreature &mob : m_state.entities().creatures().editableMobs()) {
        if (mob.dormant || !mob.effects) {
            continue;
        }
        if (const CreatureHurt hurt = tickCreature(mob); hurt.damage > 0.0) {
            hurts.append(hurt);
        }
    }

    for (const CreatureHurt &hurt : std::as_const(hurts)) {
        const QcCreature *mob = m_state.entities().creatures().find(hurt.creature);
        if (!mob) {
            continue;
        }
        // No push; a passive creature runs on the way it faces.
        const double yaw = qDegreesToRadians(static_cast<double>(mob->yaw));
        const QcVec3 behind = mob->motion.position - QcVec3{std::sin(yaw), 0.0, -std::cos(yaw)};
        QcServerCombat::hurtCreature(m_state, m_sounds, {.creature = hurt.creature, .damage = hurt.damage, .from = behind, .attacker = hurt.attacker, .knockback = 0.0});
    }
}
