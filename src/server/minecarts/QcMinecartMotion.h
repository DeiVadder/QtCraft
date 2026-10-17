#pragma once

#include "common/world/QcBlockAccess.h"
#include "server/blocks/QcRails.h"
#include "server/entities/QcMinecart.h"

#include <optional>

// How a minecart moves in one tick (decision E-382), for one cart on its own: on a rail it follows
// the track from end to end and on into the next rail whose end meets it (also up or down a slope),
// keeps its momentum, speeds up downhill and slows uphill, loses speed to drag and stops at the end of
// a track (or waits there with its speed while the chunk beyond is not loaded); off the rails it
// falls and slides to a stop like any body. Deterministic: the same world and cart give the same
// result at every place and in every rotation.
namespace QcMinecartMotion {

// Fastest speed along a track, metres per second (0.4 blocks per tick).
inline constexpr double maxSpeed = 8.0;
// Pull of gravity along a slope, metres per second squared (0.0078125 blocks per tick squared).
inline constexpr double slopeAcceleration = 3.125;
// Share of its speed a cart keeps per tick on rails: an empty one stops after about ten metres, a
// ridden one rolls far.
inline constexpr double emptyDrag = 0.96;
inline constexpr double riddenDrag = 0.997;
// Slower than this on a level track, a cart stands still.
inline constexpr double restSpeed = 0.02;
// A cart this close to a track (or closer) is on it.
inline constexpr double snapDistance = 0.5;
// A cart that stops at the end of a track stays this far inside the rail's cell.
inline constexpr double endInset = 0.02;
// A powered rail adds this much speed per second along the way the cart rolls (0.06 blocks per tick
// each tick, decision E-384) and starts a standing cart with `boostKick` away from a block at an end
// of its track. An unpowered one halves the speed each tick and stops a cart slower than `brakeStop`.
inline constexpr double boostAcceleration = 24.0;
inline constexpr double boostKick = 0.4;
inline constexpr double brakeStop = 0.6;

// The track a cart is on: the cell, its ends in the world and the direction from the first to the
// second end.
struct Segment
{
    QcBlockPos cell;
    QcRails::Track track;
    QcVec3 from;
    QcVec3 to;
    QcVec3 direction;
    double length = 0.0;
};

struct Result
{
    bool onRail = false;
    // The cart ran into the end of a track this tick and stopped there.
    bool stoppedAtEnd = false;
    // It stopped at the lower end of a slope that ends there: gravity holds it at that end, so it
    // stands like on a level track (QcMinecart::Rest).
    bool heldAtEnd = false;
    // It reached the end of a track beyond which the chunk is not loaded: it waits there with its
    // speed.
    bool waiting = false;
    // The rail it is on after the step; nothing off the rails.
    std::optional<Segment> segment;
    // It was on a powered or an unpowered activator rail in this step (decision E-384).
    bool activated = false;
    bool deactivated = false;
};

// The track the feet are on (the rail in their cell, below or above it), if one is close enough.
[[nodiscard]] std::optional<Segment> segmentAt(const QcVec3 &feet, const QcRails::Table &rails, const QcBlockAccess &world);
// Advances the cart by `dt` seconds.
Result step(QcMinecart &cart, const QcRails::Table &rails, const QcBlockAccess &world, double dt);
// The yaw (degrees, 0 looks north) of a cart moving along `direction`.
float yawAlong(const QcVec3 &direction);

} // namespace QcMinecartMotion
