// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"
#include "common/world/QcBlockAccess.h"

#include <optional>

/*! \brief Levels (0..1) of the four ambience loops, chosen from the surroundings with simple rules:
    wind under the open sky (stronger up high), a cave rumble under a thick rock ceiling, water when
    water blocks are near, rain while it rains where the listener is (muffled under a roof, faint in
    a cave).

    Sky and ceiling are averaged over the columns around the listener, water is one level for all
    water nearby (nearer blocks count more, capped at 1), so the levels change gently while the
    listener moves, even fast in a minecart. The loops cross-fade by easing the levels towards
    their targets. Glowing liquids (lava) are no water: they pop now and then instead. */
class QcAmbience
{
public:
    /*! \brief Levels (0..1) of the four ambience loops. */
    struct Levels
    {
        /*! Wind under the open sky. */
        float wind = 0.0f;
        /*! Cave rumble under a thick rock ceiling. */
        float cave = 0.0f;
        /*! Water nearby. */
        float water = 0.0f;
        /*! Rain where the listener is. */
        float rain = 0.0f;

        friend bool operator==(const Levels &, const Levels &) = default;
    };

    /*! Height (y) at which wind starts to grow, and at which it reaches its full level.
        \qcshared */
    static constexpr double calmHeight = 64.0;
    static constexpr double stormHeight = 120.0;
    /*! Wind under the open sky at calmHeight and below in dry weather: a faint breeze, so that
        the usual soundscape is no constant rush; heights and rain (storms) raise it, by up to
        rainWind at full rain, up to 1 together. */
    static constexpr double calmWind = 0.12;
    /*! Most the rain adds to the wind, at full rain. */
    static constexpr double rainWind = 0.4;
    /*! Share of the wind heard under a roof or a tree. */
    static constexpr double windUnderRoof = 0.3;
    /*! Columns sampled for sky and ceiling: every second block up to this far to each side. */
    static constexpr int shelterRadius = 2;
    /*! Opaque blocks above the head within \c {ceilingScan} that make a cave (per column).
        \qcshared */
    static constexpr int ceilingScan = 24;
    static constexpr int caveCeiling = 4;
    /*! Water blocks within the sampled box (radius 6, every second block on a grid fixed in the
        world) for the full water level; a block counts 1 - distance / waterFadeDistance.
        \qcshared */
    static constexpr int waterRadius = 6;
    static constexpr double fullWaterBlocks = 12.0;
    static constexpr double waterFadeDistance = 12.0;
    /*! Largest level change per second when fading towards a new target, the time constant of
        the easing, and the distance from the target at which a level arrives. \qcshared */
    static constexpr double fadePerSecond = 0.5;
    static constexpr double easeSeconds = 1.0;
    static constexpr float arriveLevel = 0.002f;

    /*! Share of the rain heard under a roof and in a cave. \qcshared */
    static constexpr double rainUnderRoof = 0.45;
    static constexpr double rainInCave = 0.08;

    /*! Returns the target levels in \a world for a listener whose eyes are at \a eye, while rain
        of strength \a rain (0..1; 0 where it snows or stays dry) falls there. Unloaded blocks
        count as air. */
    static Levels targets(const QcBlockAccess &world, const QcVec3 &eye, double rain = 0.0);
    /*! Returns the nearest glowing liquid (lava) among the blocks of \a world sampled for water
        around \a eye, if any. */
    [[nodiscard]] static std::optional<QcBlockPos> nearestGlowingLiquid(const QcBlockAccess &world, const QcVec3 &eye);
    /*! Chance per ambience sample that lava nearby pops. */
    static constexpr double lavaPopChance = 0.35;
    /*! Returns \a current moved towards \a target over \a seconds: a share 1 - exp(-seconds /
        easeSeconds) of the way, at most fadePerSecond * seconds per loop. */
    static Levels approach(Levels current, Levels target, double seconds);
};
