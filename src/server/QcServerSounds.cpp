#include "QcServerSounds.h"

#include "QcServerState.h"
#include "QcSoundKeys.h"

#include <algorithm>

QcServerSounds::QcServerSounds(QcServerState &state)
    : m_state(state)
{
}

void QcServerSounds::play(const QString &key, const QcVec3 &position, float volume, float pitch)
{
    const QcSoundDefinition *sound = key.isEmpty() ? nullptr : m_state.content->sound(key);
    if (!sound) {
        return;
    }

    const double range = sound->range * std::max(1.0, double(volume));
    const QcMsg::SoundEvent event{key, position, volume, pitch};

    for (const auto &client : m_state.clients) {
        if (client->state == QcServerClient::State::Playing && (client->motion.position - position).length() <= range) {
            client->connection->send(event);
        }
    }
}

void QcServerSounds::playBlock(const QcBlockDefinition &definition, QcBlockSoundAction action, QcBlockPos pos)
{
    play(QcSoundKeys::blockSound(definition, action), {pos.x + 0.5, pos.y + 0.5, pos.z + 0.5});
}
