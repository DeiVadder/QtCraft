// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"

#include <QString>

/*! \brief A sound the client should play now: from a server SoundEvent or made by the client itself
    (the player's steps, jumps, mining strikes).

    Audio output and the control interface consume it. */
struct QcSoundCue
{
    /*! Who made the sound. */
    enum class Origin {
        Server, // A SoundEvent of the server
        Local   // Made by the client itself
    };

    /*! Sound key (a content sound name). */
    QString key;
    /*! Where the sound comes from. */
    QcVec3 position;
    /*! Volume, 1 is the sound's own. */
    float volume = 1.0f;
    /*! Playback speed, 1 is the sound's own. */
    float pitch = 1.0f;
    /*! Who made the sound. */
    Origin origin = Origin::Server;
    /*! Client tick in which the cue arrived. */
    quint64 tick = 0;
};
