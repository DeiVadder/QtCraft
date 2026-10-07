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

struct MoveResult
{
    QcVec3 applied;
    bool blockedX = false;
    bool blockedY = false;
    bool blockedZ = false;
};

QcAabb boxAt(const QcVec3 &feet, double width, double height);

// True when the block blocks movement. Unloaded chunks and the space below the world are solid so
// nothing falls through missing data.
bool isSolidAt(const QcBlockAccess &world, QcBlockPos pos);
bool isLiquidAt(const QcBlockAccess &world, QcBlockPos pos);
QcBlockPos blockContaining(const QcVec3 &point);

// Moves `box` by `delta`, clipped against solid blocks (Y first, then X, then Z).
MoveResult moveBox(const QcAabb &box, const QcVec3 &delta, const QcBlockAccess &world);

// Advances any entity by one tick: `wish` is the horizontal movement direction (length <= 1),
// `speed` its target speed, `jump` jumps on ground or swims up in water. The caller updates
// motion.inWater before the call.
MoveResult stepEntity(QcEntityMotion &motion, const QcVec3 &wish, double speed, bool jump, double width, double height,
                      const QcBlockAccess &world, double dt);

// Advances a player by one tick of `dt` seconds.
void stepPlayer(QcEntityMotion &motion, const QcPlayerInput &input, const QcBlockAccess &world, double dt);

} // namespace QcPhysics
