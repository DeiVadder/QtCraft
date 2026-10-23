#include "server/core/QcServerSounds.h"

#include "common/content/definitions/QcSoundKeys.h"
#include "server/core/QcServerState.h"
#include "server/tick/QcTickScheduler.h"

#include <algorithm>

QcServerSounds::QcServerSounds(QcServerState &state, const QcTickScheduler &ticks)
    : m_state(state)
    , m_ticks(ticks)
{
}

void QcServerSounds::play(const QString &key, const QcVec3 &position, float volume, float pitch)
{
    if (m_ticks.sprinting()) {
        return;
    }

    const QcSoundDefinition *sound = key.isEmpty() ? nullptr : m_state.content().assets().sound(key);
    if (!sound) {
        return;
    }

    const double range = sound->range * std::max(1.0, static_cast<double>(volume));
    const QcMsg::SoundEvent event{key, position, volume, pitch};

    for (const auto &client : m_state.clients()) {
        if (client->state == QcServerClient::State::Playing && (client->motion.position - position).length() <= range) {
            client->connection->send(event);
        }
    }
}

void QcServerSounds::playBlock(const QcBlockDefinition &definition, QcBlockSoundAction action, QcBlockPos pos)
{
    play(QcSoundKeys::blockSound(definition, action), {pos.x + 0.5, pos.y + 0.5, pos.z + 0.5});
}
