#include "QcServerCreatures.h"

#include "QcPhysics.h"
#include "QcPlayerHealth.h"
#include "QcServerSounds.h"
#include "QcServerState.h"

QcServerCreatures::QcServerCreatures(QcServerState &state, QcServerSounds &sounds, QcPlayerHealth &health)
    : m_state(state)
    , m_sounds(sounds)
    , m_health(health)
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
    playAmbientSounds(dt);

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
