#include "server/gameplay/QcServerEffects.h"

#include "common/physics/QcPhysics.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcPlayerHealth.h"
#include "server/gameplay/QcServerCombat.h"

#include <QtMath>

#include <cmath>

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
    if (!mob || !content.effect(effect.name) || content.mobs()[mob->type].immuneTo.contains(effect.name)) {
        return false;
    }
    if (!mob->effects) {
        mob->effects.emplace();
    }
    return mob->effects->give(effect);
}

bool QcServerEffects::givePlayer(const QcContentRegistry &content, QcServerClient &client, const QcStatusEffect &effect)
{
    return !client.dead && content.effect(effect.name) && client.effects.give(effect);
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
        const QcEffectDefinition *definition = m_state.content().effect(effect.name);
        const double damage = definition && definition->hurtsAt(effect.level, effect.ticksLeft) ? definition->damageTo(client.health) : 0.0;
        if (damage > 0.0) {
            m_health.damage(client, damage, {}, QcPlayerHealth::DamageKind::Effect);
        }
        // Hunger (decision E-838): food exhaustion every tick, times the level.
        if (definition && definition->exhaustion > 0.0 && !client.dead) {
            client.hunger.exhaust(definition->exhaustion * effect.level * QcPhysics::tickDuration);
        }
    }
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

void QcServerEffects::tickCreatures()
{
    // Hurting may kill a creature (an effect of a mod without minHealth) and so change the list.
    QList<std::pair<quint32, double>> hurts;
    for (QcCreature &mob : m_state.entities().creatures().editableMobs()) {
        if (mob.dormant || !mob.effects) {
            continue;
        }
        for (const QcStatusEffect &effect : mob.effects->all()) {
            const QcEffectDefinition *definition = m_state.content().effect(effect.name);
            if (definition && definition->hurtsAt(effect.level, effect.ticksLeft) && definition->damageTo(mob.health) > 0.0) {
                hurts.append({mob.id, definition->damageTo(mob.health)});
            }
        }
        mob.effects->tick();
        if (mob.effects->isEmpty()) {
            mob.effects.reset();
        }
    }

    for (const auto &[id, damage] : std::as_const(hurts)) {
        const QcCreature *mob = m_state.entities().creatures().find(id);
        if (!mob) {
            continue;
        }
        // No push; a passive creature runs on the way it faces.
        const double yaw = qDegreesToRadians(static_cast<double>(mob->yaw));
        const QcVec3 behind = mob->motion.position - QcVec3{std::sin(yaw), 0.0, -std::cos(yaw)};
        QcServerCombat::hurtCreature(m_state, m_sounds, {.creature = id, .damage = damage, .from = behind, .knockback = 0.0});
    }
}
