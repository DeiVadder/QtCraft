#pragma once

#include "common/core/QcAabb.h"
#include "common/core/QcBlockPos.h"
#include "common/physics/QcEntityMotion.h"
#include "common/physics/QcPlayerInput.h"

#include <optional>

class QcBlockAccess;

// What the driver of a boat does in one tick (decision E-920): paddle forward or back, turn left or
// right. From the keys of the player's input: forward and back paddle, left and right (strafe) turn.
struct QcBoatControls
{
    // In [-1, 1]: forward paddles ahead, backwards brakes and paddles back slowly.
    double forward = 0.0;
    // In [-1, 1]: positive turns right (the yaw grows), negative left.
    double turn = 0.0;

    [[nodiscard]] static QcBoatControls of(const QcPlayerInput &input);

    bool operator==(const QcBoatControls &) const = default;
};

// A boat as it moves (decision E-920), the shared physics of the server (every boat, authoritative)
// and the client (the prediction of the own boat): its feet, velocity and contact (`motion.inWater`:
// it floats), the direction it points to and how fast it turns. Units like QcPhysics: metres,
// seconds, m/s; the yaw in degrees and the turn in degrees per tick.
struct QcBoatState
{
    QcEntityMotion motion;
    float yaw = 0.0f;
    float spin = 0.0f;

    // One tick of 1/20 s: the boat keeps a share of its speed and its turn (on water and in the air
    // most of it, on land that of the ground's slipperiness: little on grass, nearly all on ice),
    // the driver's `controls` (none without a driver) paddle and turn it, it floats on water and
    // falls without it, and moves, stopped by blocks.
    void step(const std::optional<QcBoatControls> &controls, const QcBlockAccess &world);

    bool operator==(const QcBoatState &) const = default;
};

// The rules of the boat's movement, in the units of the role model where they come from it (blocks
// per tick) converted to m/s.
namespace QcBoatMotion {

// Share of the horizontal velocity and of the turn a boat keeps per tick on water and in the air;
// on land the ground's slipperiness (QcBlockDefinition::slipperiness: 0.6, ice 0.98).
inline constexpr double waterKeep = 0.9;
inline constexpr double airKeep = 0.9;
// Velocity the forward key adds per tick at full press (0.04 blocks per tick per tick in the role
// model), after the boat kept its share: on water it reaches 8 m/s, on grass 2 m/s, on packed ice
// it would reach 40 m/s (maxSpeed).
inline constexpr double paddleForward = 0.8;
// Velocity the back key takes per tick, and the forward push of turning without paddling.
inline constexpr double paddleBack = 0.1;
inline constexpr double turnDrift = 0.1;
// Degrees per tick the turn grows per tick of a full press: on water the boat turns at most 10
// degrees per tick (200 degrees per second), on grass 2.5.
inline constexpr double turnPush = 1.0;
// No boat goes faster horizontally, also on the most slippery ice.
inline constexpr double maxSpeed = 40.0;
// A floating boat's feet lie this far below the water surface; its floor stays above it.
inline constexpr double draft = 0.06;
// A floating boat closes this share of the gap to its floating height per tick, at most at
// floatSpeed m/s up or down: it settles on the water within a few ticks, and a boat under water
// rises slowly to the surface.
inline constexpr double floatPull = 0.5;
inline constexpr double floatSpeed = 2.0;
// How far below the top of its cell a liquid's surface lies at its source (the same as the drawn
// surface, QcBlockRenderTable::liquidSurfaceDrop).
inline constexpr double liquidSurfaceDrop = 0.125;

// The box of a boat with its feet at `feet`.
[[nodiscard]] QcAabb boxAt(const QcVec3 &feet);
// The height of the water surface in the cell `pos`: falls with the flow's level, nearly full at a
// source, full under more water that does not fall; nothing for other blocks and for falling water
// (a waterfall carries no boat).
[[nodiscard]] std::optional<double> waterSurfaceAt(const QcBlockAccess &world, QcBlockPos pos);
// The highest water surface (waterSurfaceAt) of the cells the box overlaps; nothing without water.
[[nodiscard]] std::optional<double> waterSurfaceIn(const QcBlockAccess &world, const QcAabb &box);

} // namespace QcBoatMotion
