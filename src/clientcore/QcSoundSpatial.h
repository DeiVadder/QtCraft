#pragma once

#include "QcVec3.h"

// Loudness and stereo position of a sound at a world position, heard from a listener's ears.
// Yaw follows the game's convention: 0 looks north (-Z), 90 east (+X).
class QcSoundSpatial
{
public:
    // Share of full left/right a sound exactly to the side gets; the other ear still hears a little.
    static constexpr float panWidth = 0.85f;
    // A sound straight behind the listener loses this share of its loudness.
    static constexpr float rearDamping = 0.25f;

    struct Placement
    {
        float gain = 0.0f;
        float pan = 0.0f;
        double distance = 0.0;
    };

    // (1 - distance / range)^2, 1 at the listener and 0 from `range` on.
    static float attenuation(double distance, double range);
    // -panWidth (left) .. panWidth (right) from the horizontal direction to the source; 0 straight
    // ahead, behind, above or below.
    static float pan(const QcVec3 &listener, double yawDegrees, const QcVec3 &source);
    // Attenuation, rear damping and pan together.
    static Placement place(const QcVec3 &listener, double yawDegrees, const QcVec3 &source, double range);
};
