#include "server/mobs/QcServerCreatures.h"

#include "common/physics/QcPhysics.h"
#include "server/core/QcServerSounds.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcPlayerHealth.h"
#include "server/gameplay/QcServerCombat.h"
#include "server/mobs/QcMobCramming.h"

QcServerCreatures::QcServerCreatures(QcServerState &state, QcServerSounds &sounds, QcPlayerHealth &health, QcServerBlocks &blocks)
    : m_state(state)
    , m_sounds(sounds)
    , m_health(health)
    , m_villagers(state, sounds, blocks)
{
}

void QcServerCreatures::tick()
{
    const double dt = QcPhysics::tickDuration;
    QList<QcMobManager::PlayerView> players;
    for (const auto &client : m_state.clients()) {
        if (client->state == QcServerClient::State::Playing) {
            players.append({client->id, client->motion.position, !client->dead, !client->rules().invulnerable});
        }
    }

    const QList<QcMobManager::PlayerHit> hits =
        m_state.entities().creatures().tick(m_state.world(), players, m_state.info().worldTime, dt, [this] { return m_state.entities().newId(); });
    m_villagers.apply();
    playAmbientSounds(dt);
    applyCramming();

    for (const QcMobManager::PlayerHit &hit : hits) {
        for (const auto &client : m_state.clients()) {
            if (client->id == hit.playerEntityId) {
                m_health.damage(*client, hit.damage, hit.knockback, QcPlayerHealth::DamageKind::Armoured);
            }
        }
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
