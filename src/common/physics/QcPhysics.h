#pragma once

#include "common/core/QcAabb.h"
#include "common/physics/QcEntityMotion.h"
#include "common/physics/QcPlayerInput.h"
#include "common/world/QcBlockAccess.h"

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
// A body lying on its back (a sleeping villager, decision E-411) turns around this height above its
// feet: its feet end up this far ahead of `feet` in the direction it faces, its head behind.
inline constexpr double lyingPivot = 0.15;
// The box of a body of `width` and `height` lying on its back around `feet` and facing `yaw` (along
// an axis, as beds lie): its length behind the feet, its width across, half its width high.
QcAabb lyingBoxAt(const QcVec3 &feet, float yaw, double width, double height);

// True when the block blocks movement. Unloaded chunks inside the world's height are solid so
// nothing falls through missing data; the void below the world is open (decision E-288).
bool isSolidAt(const QcBlockAccess &world, QcBlockPos pos);
// World-space collision boxes of the block at `pos` (a unit box where data is missing, none below
// the world).
QList<QcAabb> collisionBoxesAt(const QcBlockAccess &world, QcBlockPos pos);
// Whether a box overlaps a climbable block.
bool touchesClimbable(const QcBlockAccess &world, const QcAabb &box);
// How far around its box a wall climber (a spider, decision E-451) feels for a wall, sideways (a
// body of 0.6 m in the middle of a cell beside a wall reaches it, as the paths assume), and how far
// above the first metre of its body (so a small body reaches the face of an overhang).
inline constexpr double clingReach = 0.25;
inline constexpr double clingAbove = 0.1;
// Whether a wall climber with this box touches a block beside it or above it: from a hair above its
// feet up to clingAbove over its top or its first metre, whichever is higher, grown sideways by
// clingReach.
bool clingsToWall(const QcBlockAccess &world, const QcAabb &box);
bool isLiquidAt(const QcBlockAccess &world, QcBlockPos pos);
// Whether `pos` holds water: a liquid that sets nothing alight (not lava).
bool isWaterAt(const QcBlockAccess &world, QcBlockPos pos);
// Movement factor of the liquid at `pos` (lava is slow), 1 outside liquids.
double liquidFactorAt(const QcBlockAccess &world, QcBlockPos pos);
// Smallest movement factor of the blocks a box overlaps (cobwebs), 1 without any.
double slowdownInside(const QcBlockAccess &world, const QcAabb &box);

// Slippery ground (decision E-430): on a block whose slipperiness (QcBlockDefinition, units of the
// original game) lies above `grippingSlipperiness`, a body on the ground keeps slipperiness x drag of
// its horizontal velocity per tick (the original game's rule) instead of taking its wished velocity at
// once. `footDrag` is the drag of walkers (ice 0.98 keeps 89 % per tick), `itemDrag` that of dropped
// items (96 %). A body that coasts (`coastDrag`: a minecart off the rails) keeps the whole
// slipperiness on any ground: 60 % per tick where the ground grips (it stops within a few ticks), 98 %
// on ice. The value lives with the block data (QcBlockDefinition::grippingSlipperiness).
inline constexpr double grippingSlipperiness = QcBlockDefinition::grippingSlipperiness;
inline constexpr double footDrag = 0.91;
inline constexpr double itemDrag = 0.98;
inline constexpr double coastDrag = 1.0;

// Walking speed and jump factors of the block under a body's feet (mud), and its slipperiness (ice).
struct SurfaceFactors
{
    double speed = 1.0;
    double jump = 1.0;
    double slipperiness = grippingSlipperiness;
};
SurfaceFactors surfaceUnder(const QcBlockAccess &world, const QcVec3 &feet);
QcBlockPos blockContaining(const QcVec3 &point);

// Moves `box` by `delta`, clipped against solid blocks (Y first, then X, then Z).
MoveResult moveBox(const QcAabb &box, const QcVec3 &delta, const QcBlockAccess &world);

// What an entity wants for one tick: `wish` is the horizontal movement direction (length <= 1),
// `speed` its target speed, `jump` jumps on ground, swims up in water or climbs, `hold` keeps a
// climber in place, `slideDrag` is how it slides on slippery ground (footDrag, itemDrag, coastDrag).
// A body that `climbsWalls` (spiders) holds on to any wall it touches (clingsToWall) as to a ladder.
struct Steering
{
    QcVec3 wish;
    double speed = 0.0;
    bool jump = false;
    bool hold = false;
    double slideDrag = footDrag;
    bool climbsWalls = false;
};

// Share of its horizontal velocity a body on ground of `slipperiness` keeps per tick of `dt`
// seconds: (slipperiness x drag) per 1/20 s on slippery ground or for a body that coasts, else 0.
double slideKeep(double slipperiness, double drag, double dt);

// Width and height of an entity's collision box.
struct BodySize
{
    double width = playerWidth;
    double height = playerHeight;
};

// The steps of a body are methods of QcEntityMotion (step, stepPlayer).

} // namespace QcPhysics
