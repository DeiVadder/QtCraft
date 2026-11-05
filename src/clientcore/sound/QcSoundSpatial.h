// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/sound/QcSoundCue.h"
#include "common/content/registry/QcSoundDefinition.h"
#include "common/core/QcVec3.h"

/*! \brief Loudness and stereo position of a sound at a world position, heard from a listener's
    ears.

    Yaw follows the game's convention: 0 looks north (-Z), 90 east (+X). */
class QcSoundSpatial
{
public:
    /*! Share of full left/right a sound exactly to the side gets; the other ear still hears a
        little. */
    static constexpr float panWidth = 0.85f;
    /*! A sound straight behind the listener loses this share of its loudness. */
    static constexpr float rearDamping = 0.25f;

    /*! \brief How a sound is heard. */
    struct Placement
    {
        /*! Loudness (0..1) from distance and rear damping. */
        float gain = 0.0f;
        /*! Stereo position: -panWidth left .. panWidth right. */
        float pan = 0.0f;
        /*! Distance from the listener in metres. */
        double distance = 0.0;
    };

    /*! \brief Where the listener's ears are and which way they look. */
    struct Listener
    {
        /*! Position of the ears in the world. */
        QcVec3 ears;
        /*! Yaw the listener looks along, in degrees. */
        double yawDegrees = 0.0;
    };

    /*! \brief A positional sound as the engine places it: where it sounds, how loud at the
        source (the definition's volume times the cue's) and how far it carries (the definition's
        range, longer for cues louder than 1, as the server sends them). \qcfields */
    struct Source
    {
        QcVec3 position;
        float volume = 1.0f;
        double range = 0.0;
    };

    /*! Returns (1 - \a distance / \a range)^2: 1 at the listener and 0 from \a range on. */
    static float attenuation(double distance, double range);
    /*! Returns the stereo position -panWidth (left) .. panWidth (right) of \a source for a
        \a listener looking along \a yawDegrees, from the horizontal direction to the source; 0
        straight ahead, behind, above or below. */
    static float pan(const QcVec3 &listener, double yawDegrees, const QcVec3 &source);
    /*! Returns attenuation, rear damping and pan together of \a source within \a range for a
        \a listener looking along \a yawDegrees. */
    static Placement place(const QcVec3 &listener, double yawDegrees, const QcVec3 &source, double range);

    /*! Returns the source of the cue \a cue of the sound \a sound. */
    static Source source(const QcSoundDefinition &sound, const QcSoundCue &cue);
    /*! Returns the placement of \a source for \a listener, its gain including the source's
        volume: what a voice of that sound plays with, when it starts and whenever the listener
        or source moved. */
    static Placement hear(const Source &source, const Listener &listener);
};
