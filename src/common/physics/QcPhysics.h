#pragma once

#include "QcAabb.h"
#include "QcBlockAccess.h"
#include "QcEntityMotion.h"
#include "QcPlayerInput.h"

// Deterministic movement and collision shared by server (authoritative) and client (prediction).
// Units: metres, seconds, metres per second.
namespace QcPhysics {

inline constexpr double tickRate = 20.0;
inline constexpr double tickDuration = 1.0 / tickRate;

inline constexpr double playerWidth = 0.6;
inline constexpr double playerHeight = 1.8;
inline constexpr double playerEyeHeight = 1.62;

inline constexpr double gravity = 28.0;
inline constexpr double terminalVelocity = 60.0;
inline constexpr double jumpVelocity = 9.0;
inline constexpr double walkSpeed = 4.3;
inline constexpr double sprintSpeed = 5.6;
inline constexpr double sneakSpeed = 1.3;
inline constexpr double groundAcceleration = 20.0;
inline constexpr double airAcceleration = 4.0;

inline constexpr double swimSpeed = 2.2;
inline constexpr double waterAcceleration = 8.0;
inline constexpr double waterGravity = 6.0;
inline constexpr double waterSinkSpeed = 2.0;
inline constexpr double swimUpSpeed = 3.0;
inline constexpr double swimUpAcceleration = 18.0;

// Creative flight (decision E-284): horizontal speed, with sprint, and the speed up and down with
// jump and sneak; the velocity eases towards them like on the ground, without gravity.
inline constexpr double flySpeed = 10.9;
inline constexpr double flySprintSpeed = 21.6;
inline constexpr double flyVerticalSpeed = 7.5;
inline constexpr double flyAcceleration = 10.0;

// Bodies on the ground walk up steps this high without jumping (slabs, stairs).
inline constexpr double stepHeight = 0.5;

// On ladders: speed upwards while pushing against the wall or jumping, and the fastest slide down.
inline constexpr double climbSpeed = 2.4;
inline constexpr double climbDownSpeed = 2.0;

struct MoveResult
{
    QcVec3 applied;
    bool blockedX = false;
    bool blockedY = false;
    bool blockedZ = false;
};

QcAabb boxAt(const QcVec3 &feet, double width, double height);

// True when the block blocks movement. Unloaded chunks inside the world's height are solid so
// nothing falls through missing data; the void below the world is open (decision E-288).
bool isSolidAt(const QcBlockAccess &world, QcBlockPos pos);
// World-space collision boxes of the block at `pos` (a unit box where data is missing, none below
// the world).
QList<QcAabb> collisionBoxesAt(const QcBlockAccess &world, QcBlockPos pos);
// Whether a box overlaps a climbable block.
bool touchesClimbable(const QcBlockAccess &world, const QcAabb &box);
bool isLiquidAt(const QcBlockAccess &world, QcBlockPos pos);
// Movement factor of the liquid at `pos` (lava is slow), 1 outside liquids.
double liquidFactorAt(const QcBlockAccess &world, QcBlockPos pos);
// Smallest movement factor of the blocks a box overlaps (cobwebs), 1 without any.
double slowdownInside(const QcBlockAccess &world, const QcAabb &box);

// Walking speed and jump factors of the block under a body's feet (mud).
struct SurfaceFactors
{
    double speed = 1.0;
    double jump = 1.0;
};
SurfaceFactors surfaceUnder(const QcBlockAccess &world, const QcVec3 &feet);
QcBlockPos blockContaining(const QcVec3 &point);

// Moves `box` by `delta`, clipped against solid blocks (Y first, then X, then Z).
MoveResult moveBox(const QcAabb &box, const QcVec3 &delta, const QcBlockAccess &world);

// What an entity wants for one tick: `wish` is the horizontal movement direction (length <= 1),
// `speed` its target speed, `jump` jumps on ground, swims up in water or climbs, `hold` keeps a
// climber in place.
struct Steering
{
    QcVec3 wish;
    double speed = 0.0;
    bool jump = false;
    bool hold = false;
};

// Width and height of an entity's collision box.
struct BodySize
{
    double width = playerWidth;
    double height = playerHeight;
};

// Advances any entity by one tick. The caller updates motion.inWater before the call.
MoveResult stepEntity(QcEntityMotion &motion, const Steering &steering, BodySize size, const QcBlockAccess &world, double dt);

// Advances a player by one tick of `dt` seconds. `mayFly` comes from the player's game mode
// (QcGameModes::Rules); without it a wish to fly is ignored and a flight ends.
void stepPlayer(QcEntityMotion &motion, const QcPlayerInput &input, const QcBlockAccess &world, double dt, bool mayFly);

} // namespace QcPhysics
