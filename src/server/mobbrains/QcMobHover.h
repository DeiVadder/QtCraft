// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/physics/QcPhysics.h"

#include <optional>

class QcBlockAccess;
struct QcCreature;
struct QcFamilyDefinition;
struct QcMobDefinition;

/*! \brief How hovering creatures move (QcMobDefinition::hover, decision E-839; the Kjut): without
    gravity, at their hover height above the ground or the liquid below them (bobbing gently), lower
    under a low ceiling and never higher than their highest hover above the ground; they fly towards
    where their brain wants to go, rise over a wall in their way and turn slowly to face where they
    go.

    Children of a family that chase nobody orbit their parent (orbitPoint). */
namespace QcMobHover {

/*! Vertical speed per metre off the wanted height in 1/s. */
inline constexpr double verticalGain = 5.0;
/*! How fast the velocity follows the wish (share per second). */
inline constexpr double steerRate = 6.0;
/*! Ticks a hoverer keeps rising after a wall stopped it. */
inline constexpr int climbTicks = 10;
/*! How far above its height a hoverer rises after a wall stopped it. */
inline constexpr double climbHeight = 1.2;
/*! Room left between the head and a ceiling. */
inline constexpr double ceilingGap = 0.05;
/*! How fast a speed upwards beyond the one towards its height fades (a split's leap), in m/s²:
    slower than gravity, so a child split off visibly pops up. */
inline constexpr double liftFade = 12.0;

/*! \brief The floor (top of the first block with a collision box or a liquid below the middle of
    the body) and the ceiling (bottom of the first block with a collision box above the head)
    around a body whose feet are at \c {feet}; no floor within reach lies \c {reach} below the
    feet. */
struct Room
{
    /*! The height of the floor. */
    double floor = 0.0;
    /*! The height of the ceiling, if any. */
    std::optional<double> ceiling;
};
/*! Returns the room in \a world around a body of \a height whose feet are at \a feet, looking for
    a floor within \a reach; with \a sinksInWater water is no floor. */
[[nodiscard]] Room roomAt(const QcBlockAccess &world, const QcVec3 &feet, double height, double reach, bool sinksInWater);

/*! Returns where \a mob, a hoverer of the kind \a definition in \a room, wants its feet this
    tick: its hover height above the floor with the bob at \a seconds, higher while it climbs,
    below the ceiling, never above its highest hover. */
[[nodiscard]] double wantedFeet(const QcCreature &mob, const QcMobDefinition &definition, const Room &room, double seconds);

/*! \brief What a hoverer does this tick: the steering of its brain (a horizontal wish and a
    speed) and, for an orbiting child, the height its feet keep instead of the hover height. */
struct Course
{
    /*! The steering of its brain. */
    QcPhysics::Steering steering;
    /*! The height its feet keep, for an orbiting child. */
    std::optional<double> feetY;
};

/*! Returns the vertical speed of a hoverer this tick from \a velocity the tick before and
    \a rise, the speed towards its height: a speed upwards beyond \a rise (a split's leap) fades
    (liftFade), otherwise it follows \a rise; while \a knockedBack it falls with gravity, but at
    most at the speed \a rise would sink it and never while below its height. */
[[nodiscard]] double vertical(double velocity, double rise, bool knockedBack);

/*! One tick of \a mob, a hoverer of the kind \a definition, on \a course at the clock \a seconds
    (the bob): it steers, keeps its height and moves through \a world; a knocked-back one does
    not steer, one without a mind floats where it is. */
void fly(QcCreature &mob, const QcMobDefinition &definition, const QcBlockAccess &world, const Course &course, double seconds);

/*! Returns where the child \a child orbits its parent with its feet at \a parentFeet at
    \a seconds: around the middle of the parent's body (\a parentHeight), on a radius of
    \a family and with a phase and direction of its own (from its id). */
[[nodiscard]] QcVec3 orbitPoint(quint32 child, const QcVec3 &parentFeet, double parentHeight, const QcFamilyDefinition &family,
                                double seconds);

/*! Returns the course of the child \a mob towards its orbit point \a point (its feet), no faster
    than \a speed. */
[[nodiscard]] Course towards(const QcCreature &mob, const QcVec3 &point, double speed);

/*! Returns \a from turned towards \a to by at most \a degrees (yaws in degrees). */
[[nodiscard]] float turned(float from, float to, double degrees);

} // namespace QcMobHover
