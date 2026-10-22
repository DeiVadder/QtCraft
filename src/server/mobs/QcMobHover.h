#pragma once

#include "common/physics/QcPhysics.h"

#include <optional>

class QcBlockAccess;
struct QcCreature;
struct QcFamilyDefinition;
struct QcMobDefinition;

// How hovering creatures move (QcMobDefinition::hover, decision E-839; the Kjut): without gravity,
// at their hover height above the ground or the liquid below them (bobbing gently), lower under a
// low ceiling and never higher than their highest hover above the ground; they fly towards where
// their brain wants to go, rise over a wall in their way and turn slowly to face where they go.
// Children of a family that chase nobody orbit their parent (orbitPoint).
namespace QcMobHover {

// Vertical speed per metre off the wanted height in 1/s, and the most in m/s.
inline constexpr double verticalGain = 5.0;
inline constexpr double maxVerticalSpeed = 3.0;
// How fast the velocity follows the wish (share per second).
inline constexpr double steerRate = 6.0;
// Ticks a hoverer keeps rising after a wall stopped it, and how far above its height it rises.
inline constexpr int climbTicks = 10;
inline constexpr double climbHeight = 1.2;
// Room left between the head and a ceiling.
inline constexpr double ceilingGap = 0.05;
// How fast a speed upwards beyond the one towards its height fades (a split's leap), in m/s²:
// slower than gravity, so a child split off visibly pops up.
inline constexpr double liftFade = 12.0;

// The floor (top of the first block with a collision box or a liquid below the middle of the body)
// and the ceiling (bottom of the first block with a collision box above the head) around a body
// whose feet are at `feet`; no floor within reach lies `reach` below the feet.
struct Room
{
    double floor = 0.0;
    std::optional<double> ceiling;
};
[[nodiscard]] Room roomAt(const QcBlockAccess &world, const QcVec3 &feet, double height, double reach);

// Where a hoverer wants its feet this tick: its hover height above the floor with the bob at
// `seconds`, higher while it climbs, below the ceiling, never above its highest hover.
[[nodiscard]] double wantedFeet(const QcCreature &mob, const QcMobDefinition &definition, const Room &room, double seconds);

// What a hoverer does this tick: the steering of its brain (a horizontal wish and a speed) and,
// for an orbiting child, the height its feet keep instead of the hover height.
struct Course
{
    QcPhysics::Steering steering;
    std::optional<double> feetY;
};

// The vertical speed of a hoverer this tick from `velocity` the tick before and `rise`, the speed
// towards its height: a speed upwards beyond `rise` (a split's leap) fades (liftFade), otherwise it
// follows `rise`; while `knockedBack` it falls with gravity, but at most at the speed `rise` would
// sink it and never while below its height.
[[nodiscard]] double vertical(double velocity, double rise, bool knockedBack);

// One tick of a hoverer at the clock `seconds` (the bob): it steers, keeps its height and moves
// through the world; a knocked-back one does not steer, one without a mind floats where it is.
void fly(QcCreature &mob, const QcMobDefinition &definition, const QcBlockAccess &world, const Course &course, double seconds);

// Where the child `child` orbits its parent at `seconds`: around the middle of the parent's body
// (`parentHeight`), on a radius and with a phase and direction of its own (from its id).
[[nodiscard]] QcVec3 orbitPoint(quint32 child, const QcVec3 &parentFeet, double parentHeight, const QcFamilyDefinition &family,
                                double seconds);

// The course of a child towards its orbit point `point` (its feet), no faster than `speed`.
[[nodiscard]] Course towards(const QcCreature &mob, const QcVec3 &point, double speed);

// `from` turned towards `to` by at most `degrees` (yaws in degrees).
[[nodiscard]] float turned(float from, float to, double degrees);

} // namespace QcMobHover
