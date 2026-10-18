#include "server/mobs/QcServerCreatures.h"

#include "common/physics/QcPhysics.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcPlayerHealth.h"
#include "server/gameplay/QcServerCombat.h"
#include "server/gameplay/QcServerEffects.h"
#include "server/mobs/QcMobCramming.h"
#include "server/projectiles/QcProjectileFlight.h"

QcServerCreatures::QcServerCreatures(QcServerState &state, QcServerSounds &sounds, QcPlayerHealth &health, QcServerBlocks &blocks)
    : m_state(state)
    , m_sounds(sounds)
    , m_health(health)
    , m_villagers(state, sounds, blocks)
    , m_traits(state, sounds, blocks)
    , m_infestation(state, blocks)
{
}

void QcServerCreatures::tick()
{
    const double dt = QcPhysics::tickDuration;
    QList<QcMobManager::PlayerView> players;
    for (const auto &client : m_state.clients()) {
        if (client->state == QcServerClient::State::Playing) {
            players.append({.entityId = client->id,
                            .position = client->motion.position,
                            .alive = !client->dead,
                            .targetable = !client->rules().invulnerable,
                            .look = QcProjectileFlight::direction(client->yaw, client->pitch),
                            .name = client->name});
        }
    }

    m_state.entities().creatures().setGriefing(m_state.info().mobGriefing);
    // Before the creatures move, which forgets who was hurt.
    m_infestation.tick();
    const QList<QcMobManager::PlayerHit> hits =
        m_state.entities().creatures().tick(m_state.world(), players, m_state.info().worldTime, dt, [this] { return m_state.entities().newId(); });
    m_villagers.apply();
    m_traits.apply();
    playAmbientSounds(dt);
    applyCreatureHits();
    applyCramming();
    applyPlayerHits(hits);
}

void QcServerCreatures::applyPlayerHits(const QList<QcMobManager::PlayerHit> &hits)
{
    for (const QcMobManager::PlayerHit &hit : hits) {
        for (const auto &client : m_state.clients()) {
            if (client->id == hit.playerEntityId) {
                strike(*client, hit);
            }
        }
    }
}

void QcServerCreatures::applyCreatureHits()
{
    const QList<QcMobManager::CreatureHit> hits = m_state.entities().creatures().takeCreatureHits();
    for (const QcMobManager::CreatureHit &hit : hits) {
        const QcCreature *attacker = m_state.entities().creatures().find(hit.attacker);
        const QcCreature *target = m_state.entities().creatures().find(hit.target);
        if (!attacker || !target) {
            continue;
        }
        const QString sound = m_state.content().mobs()[attacker->type].golem.attackSound;
        const QcVec3 at = target->motion.position + QcVec3{0.0, m_state.content().mobs()[target->type].height * 0.5, 0.0};
        if (QcServerCombat::hurtCreature(m_state, m_sounds, {.creature = hit.target, .damage = hit.damage, .from = hit.from, .lift = hit.lift}) && !sound.isEmpty()) {
            m_sounds.play(sound, at);
        }
    }
}

void QcServerCreatures::strike(QcServerClient &client, const QcMobManager::PlayerHit &hit)
{
    const QcHurtWindow::Take take = m_health.damage(client, hit.damage, hit.knockback, QcPlayerHealth::DamageKind::Armoured);
    // A golem's blow throws higher than the usual push lifts.
    if (take == QcHurtWindow::Take::Fresh && hit.lift != 1.0 && hit.knockback.length() > 0.0) {
        client.motion.velocity.y = QcMobManager::knockbackLift * hit.lift;
        client.stateDirty = true;
    }

    // The effects of the blow (a cave spider's poison) only when the blow counts.
    if (take == QcHurtWindow::Take::Ignored || client.dead || hit.mobType < 0 || hit.mobType >= m_state.content().mobs().size()) {
        return;
    }
    for (const QcStatusEffect &effect : m_state.content().mobs()[hit.mobType].blowEffects) {
        QcServerEffects::givePlayer(m_state.content(), client, effect);
    }
}

void QcServerCreatures::playAmbientSounds(double dt)
{
    for (const QcCreature &mob : m_state.entities().creatures().mobs()) {
        const QcMobDefinition &definition = m_state.content().mobs()[mob.type];
        if (!mob.dormant && !definition.ambientSound.isEmpty() && m_state.soundRandom().generateDouble() < dt / ambientSeconds) {
            m_sounds.play(definition.ambientSound, mob.motion.position + QcVec3{0.0, definition.height * 0.8, 0.0});
        }
    }
}

void QcServerCreatures::announceTeleports()
{
    m_traits.announceTeleports();
}

void QcServerCreatures::applyCramming()
{
    const QList<QcMobCramming::Hurt> hurt =
        QcMobCramming::apply(m_state.entities().creatures(), m_state.info().maxEntityCramming, m_state.clock().tick());
    int sounds = 0;
    for (const QcMobCramming::Hurt &one : hurt) {
        const QcMobDefinition &definition = m_state.content().mobs()[one.type];
        const QcVec3 centre = one.position + QcVec3{0.0, definition.height * 0.5, 0.0};
        if (sounds++ < crammingSoundsPerTick) {
            m_sounds.play(one.drops ? definition.deathSound : definition.hurtSound, centre);
        }
        if (one.drops) {
            m_state.entities().noteDeath(one.id);
            QcServerCombat::dropLoot(m_state, *one.drops, centre);
        }
    }
}
