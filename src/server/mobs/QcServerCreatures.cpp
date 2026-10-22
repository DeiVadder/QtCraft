#include "server/mobs/QcServerCreatures.h"

#include "common/physics/QcPhysics.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcPlayerHealth.h"
#include "server/gameplay/QcServerCombat.h"
#include "server/gameplay/QcServerEffects.h"
#include "server/mobs/QcMobCramming.h"
#include "server/mobs/QcPetOwners.h"
#include "server/projectiles/QcProjectileFlight.h"

#include <cmath>

QcServerCreatures::QcServerCreatures(QcServerState &state, QcServerSounds &sounds, QcPlayerHealth &health, QcServerBlocks &blocks)
    : m_state(state)
    , m_sounds(sounds)
    , m_health(health)
    , m_villagers(state, sounds, blocks)
    , m_traits(state, sounds, blocks)
    , m_infestation(state, blocks)
    , m_spawners(state)
    , m_guardians(state, m_spawners)
    , m_crumbling(state, sounds, blocks)
    , m_drinking(state, sounds)
    , m_families(state, sounds)
{
}

QList<QcMobManager::PlayerView> QcServerCreatures::playerViews() const
{
    // Only fresh foes count: a pet takes up what its owner fought in the last few seconds.
    const quint64 now = m_state.clock().tick();
    const auto fresh = [now](quint32 foe, quint64 tick) { return now - tick <= foeTicks ? foe : 0; };
    QList<QcMobManager::PlayerView> players;
    for (const auto &client : m_state.clients()) {
        if (client->state == QcServerClient::State::Playing) {
            const QcItemStack &held = client->inventory.slot(client->inventory.selectedHotbarSlot());
            const QcVec3 velocity = client->motion.velocity;
            players.append({.entityId = client->id,
                            .position = client->motion.position,
                            .alive = client->hasBody(),
                            .targetable = !client->rules().invulnerable,
                            .look = QcProjectileFlight::direction(client->yaw, client->pitch),
                            .name = client->name,
                            .identity = QcPetOwners::keyOf(*client),
                            .hurtBy = fresh(client->foes.hurtBy, client->foes.hurtTick),
                            .struck = fresh(client->foes.struck, client->foes.struckTick),
                            .held = held.isEmpty() ? qcNoItem : held.item,
                            .effects = &client->effects,
                            .speed = std::hypot(velocity.x, velocity.z),
                            .hurtTick = client->foes.hurtTick,
                            .struckTick = client->foes.struckTick});
        }
    }
    return players;
}

void QcServerCreatures::tick()
{
    const double dt = QcPhysics::tickDuration;
    const QList<QcMobManager::PlayerView> players = playerViews();
    m_state.entities().creatures().setGriefing(m_state.info().mobGriefing);
    m_state.entities().creatures().setWeather(m_state.info().weather.rain, m_state.info().weather.thunder);
    // Before the creatures move, which forgets who was hurt.
    m_infestation.tick();
    m_families.tick();
    const QList<QcMobManager::PlayerHit> hits =
        m_state.entities().creatures().tick(m_state.world(), players, m_state.info().worldTime, dt, [this] { return m_state.entities().newId(); });
    m_villagers.apply();
    m_traits.apply();
    playAmbientSounds(dt);
    applyCreatureHits();
    applyCramming();
    applyPlayerHits(hits);
    m_spawners.tick();
    m_spawners.announceChanges();
    m_guardians.tick();
    m_crumbling.tick();
    m_drinking.tick();
}

void QcServerCreatures::applyPlayerHits(const QList<QcMobManager::PlayerHit> &hits)
{
    for (const QcMobManager::PlayerHit &hit : hits) {
        for (const auto &client : m_state.clients()) {
            if (client->id != hit.playerEntityId) {
                continue;
            }
            strike(*client, hit);
            // The player's pets turn on the creature (decision E-480).
            if (hit.mob != 0) {
                client->foes.hurtBy = hit.mob;
                client->foes.hurtTick = m_state.clock().tick();
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
