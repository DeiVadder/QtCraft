#pragma once

#include "QcBlockDefinition.h"
#include "QcVec3.h"

#include <QString>

class QcServerState;
class QcTickScheduler;

// Sends sound events to the clients that can hear them.
class QcServerSounds
{
public:
    QcServerSounds(QcServerState &state, const QcTickScheduler &ticks);

    // Sends a sound to every playing client within the sound's range (its definition's range, longer
    // for louder events). Unknown sounds are not sent, nor any sound during a sprint: it skips game
    // time, its sounds would only be a burst of noise (decision E-249).
    void play(const QString &key, const QcVec3 &position, float volume = 1.0f, float pitch = 1.0f);
    void playBlock(const QcBlockDefinition &definition, QcBlockSoundAction action, QcBlockPos pos);

private:
    QcServerState &m_state;
    const QcTickScheduler &m_ticks;
};
