#pragma once

#include "QcBlockDefinition.h"
#include "QcVec3.h"

#include <QString>

struct QcServerState;

// Sends sound events to the clients that can hear them.
class QcServerSounds
{
public:
    explicit QcServerSounds(QcServerState &state);

    // Sends a sound to every playing client within the sound's range (its definition's range, longer
    // for louder events). Unknown sounds are not sent.
    void play(const QString &key, const QcVec3 &position, float volume = 1.0f, float pitch = 1.0f);
    void playBlock(const QcBlockDefinition &definition, QcBlockSoundAction action, QcBlockPos pos);

private:
    QcServerState &m_state;
};
