#pragma once

#include "common/core/QcVec3.h"

#include <QString>

// A sound the client should play now: from a server SoundEvent or made by the client itself
// (the player's steps, jumps, mining strikes). Audio output and the control interface consume it.
struct QcSoundCue
{
    enum class Origin { Server, Local };

    QString key;
    QcVec3 position;
    float volume = 1.0f;
    float pitch = 1.0f;
    Origin origin = Origin::Server;
    // Client tick in which the cue arrived.
    quint64 tick = 0;
};
