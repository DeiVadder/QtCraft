#include "common/physics/QcBoatMotion.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcWorldConstants.h"
#include "common/physics/QcPhysics.h"
#include "common/world/QcBlockAccess.h"
#include "common/world/QcBoatBody.h"

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// A boat this far above the water surface still counts as floating: it settles onto it instead of
// falling the last centimetres.
constexpr double floatReach = 0.2;

// The share of its speed and turn a boat keeps in this tick: floating or in the air the role model's,
// on the ground that of the block under it.
double keepOf(const QcEntityMotion &motion, bool floating, const QcBlockAccess &world)
{
    if (floating) {
        return QcBoatMotion::waterKeep;
    }
    if (motion.onGround) {
        return QcPhysics::surfaceUnder(world, motion.position).slipperiness;
    }
    return QcBoatMotion::airKeep;
}

// The vertical velocity after this tick: towards the floating height on water, gravity without.
double verticalVelocity(const QcEntityMotion &motion, std::optional<double> surface)
{
    if (surface) {
        const double target = *surface - QcBoatMotion::draft;
        const double pull = (target - motion.position.y) * QcBoatMotion::floatPull * QcPhysics::tickRate;
        return std::clamp(pull, -QcBoatMotion::floatSpeed, QcBoatMotion::floatSpeed);
    }
    return std::max(motion.velocity.y - QcPhysics::gravity * QcPhysics::tickDuration, -QcPhysics::terminalVelocity);
}

// What the driver's paddle does in one tick: the turn it adds (degrees per tick) and the velocity it
// adds along the boat's direction.
struct Stroke
{
    double turn = 0.0;
    double push = 0.0;
};

Stroke strokeOf(QcBoatControls controls)
{
    Stroke stroke{controls.turn * QcBoatMotion::turnPush, 0.0};
    if (controls.forward > 0.0) {
        stroke.push = controls.forward * QcBoatMotion::paddleForward;
    } else if (controls.forward < 0.0) {
        stroke.push = controls.forward * QcBoatMotion::paddleBack;
    } else if (controls.turn != 0.0) {
        stroke.push = std::abs(controls.turn) * QcBoatMotion::turnDrift;
    }
    return stroke;
}

// The horizontal velocity no faster than QcBoatMotion::maxSpeed.
QcVec3 limited(const QcVec3 &velocity)
{
    const double speed = std::hypot(velocity.x, velocity.z);
    if (speed <= QcBoatMotion::maxSpeed) {
        return velocity;
    }
    const double scale = QcBoatMotion::maxSpeed / speed;
    return {velocity.x * scale, velocity.y, velocity.z * scale};
}

// Whether a water state falls (a waterfall, fed from above).
bool isFalling(const QcContentRegistry &content, QcBlockId block)
{
    return content.property(block, u"falling"_s) == u"true"_s;
}

} // namespace

namespace {

// The highest water surface in the column of cells from `bottom` up to the height `top`.
std::optional<double> highestIn(const QcBlockAccess &world, QcBlockPos bottom, int top)
{
    std::optional<double> highest;
    for (int y = bottom.y; y <= top; ++y) {
        const std::optional<double> surface = QcBoatMotion::waterSurfaceAt(world, {bottom.x, y, bottom.z});
        if (surface && (!highest || *surface > *highest)) {
            highest = surface;
        }
    }
    return highest;
}

} // namespace

QcBoatControls QcBoatControls::of(const QcPlayerInput &input)
{
    const auto clamped = [](float value) { return std::isfinite(value) ? std::clamp(static_cast<double>(value), -1.0, 1.0) : 0.0; };
    return {clamped(input.forward), clamped(input.strafe)};
}

void QcBoatState::step(const std::optional<QcBoatControls> &controls, const QcBlockAccess &world)
{
    const QcAabb box = QcBoatMotion::boxAt(motion.position);
    const std::optional<double> surface = QcBoatMotion::waterSurfaceIn(world, box);
    const bool floating = surface && *surface > motion.position.y - floatReach;

    const double keep = keepOf(motion, floating, world);
    motion.velocity.x *= keep;
    motion.velocity.z *= keep;
    spin = static_cast<float>(spin * keep);
    motion.velocity.y = verticalVelocity(motion, floating ? surface : std::nullopt);

    // The paddle turns the boat first, then pushes it along its new direction.
    const Stroke stroke = controls ? strokeOf(*controls) : Stroke{};
    spin = static_cast<float>(spin + stroke.turn);
    yaw = qcWrappedYaw(yaw + spin);
    motion.velocity += QcBoatBody::forward(yaw) * stroke.push;
    motion.velocity = limited(motion.velocity);

    // Blocks stop it along the axis they block; a boat never steps up (it climbs no bank or waterfall).
    const QcPhysics::MoveResult move = QcPhysics::moveBox(box, motion.velocity * QcPhysics::tickDuration, world);
    motion.position += move.applied;
    motion.onGround = move.blockedY && motion.velocity.y < 0.0;
    motion.inWater = floating;
    if (move.blockedX) {
        motion.velocity.x = 0.0;
    }
    if (move.blockedY) {
        motion.velocity.y = 0.0;
    }
    if (move.blockedZ) {
        motion.velocity.z = 0.0;
    }
    // Deep in the void it stops sinking (decision E-288), and it stays inside the world's border.
    motion.position.y = std::max(motion.position.y, QcWorld::voidFloorY);
    motion.position.x = std::clamp(motion.position.x, QcWorld::lowestBody, QcWorld::highestBody);
    motion.position.z = std::clamp(motion.position.z, QcWorld::lowestBody, QcWorld::highestBody);
}

namespace QcBoatMotion {

QcAabb boxAt(const QcVec3 &feet)
{
    return QcPhysics::boxAt(feet, QcBoatBody::width, QcBoatBody::height);
}

std::optional<double> waterSurfaceAt(const QcBlockAccess &world, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    const QcContentRegistry &content = world.content();
    if (!block || !content.isValidBlock(*block) || !content.block(*block).isWater()) {
        return std::nullopt;
    }
    if (isFalling(content, *block)) {
        return std::nullopt;
    }
    // Under more water the cell is full; a waterfall pouring in does not lift the surface.
    const std::optional<QcBlockId> above = world.blockAt(pos.offset(0, 1, 0));
    if (above && content.isValidBlock(*above) && content.block(*above).isWater() && !isFalling(content, *above)) {
        return pos.y + 1.0;
    }

    const int distance = content.block(*block).flow.distance;
    const int level = content.property(*block, u"level"_s).toInt();
    const double share = distance <= 0 ? 1.0 : 1.0 - static_cast<double>(level) / static_cast<double>(distance + 1);
    return pos.y + (1.0 - liquidSurfaceDrop) * std::clamp(share, 0.0, 1.0);
}

std::optional<double> waterSurfaceIn(const QcBlockAccess &world, const QcAabb &box)
{
    constexpr double inset = 1e-6;
    const QcBlockPos low = QcPhysics::blockContaining(box.min + QcVec3{inset, 0.0, inset});
    const QcBlockPos high = QcPhysics::blockContaining(box.max - QcVec3{inset, inset, inset});
    std::optional<double> highest;
    for (int x = low.x; x <= high.x; ++x) {
        for (int z = low.z; z <= high.z; ++z) {
            const std::optional<double> surface = highestIn(world, {x, low.y, z}, high.y);
            if (surface && (!highest || *surface > *highest)) {
                highest = surface;
            }
        }
    }
    return highest;
}

} // namespace QcBoatMotion
