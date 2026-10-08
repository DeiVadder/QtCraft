#pragma once

#include "QcBlockAccess.h"
#include "QcVec3.h"

// Levels (0..1) of the three ambience loops, chosen from the surroundings with simple rules:
// wind under the open sky (stronger up high), a cave rumble under a thick rock ceiling, water
// when water blocks are near. The loops cross-fade by moving the levels gradually.
class QcAmbience
{
public:
    struct Levels
    {
        float wind = 0.0f;
        float cave = 0.0f;
        float water = 0.0f;

        friend bool operator==(const Levels &, const Levels &) = default;
    };

    // Height (y) at which wind starts to grow, and at which it reaches its full level.
    static constexpr double calmHeight = 64.0;
    static constexpr double stormHeight = 120.0;
    // Opaque blocks above the head within `ceilingScan` that make a cave.
    static constexpr int ceilingScan = 24;
    static constexpr int caveCeiling = 4;
    // Water blocks within the sampled box (radius 6, every second block) for the full water level.
    static constexpr int waterRadius = 6;
    static constexpr int fullWaterBlocks = 12;
    // Level change per second when fading towards a new target.
    static constexpr double fadePerSecond = 0.5;

    // Target levels for a listener whose eyes are at `eye`. Unloaded blocks count as air.
    static Levels targets(const QcBlockAccess &world, const QcVec3 &eye);
    // Moves `current` towards `target` by at most fadePerSecond * seconds per loop.
    static Levels approach(Levels current, Levels target, double seconds);
};
