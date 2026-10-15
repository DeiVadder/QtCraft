#include "QcMinecartMotion.h"

#include "QcMinecartBody.h"
#include "QcPhysics.h"
#include "QcStandPose.h"

#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>

namespace QcMinecartMotion {

namespace {

// More rails than a cart can cross in one tick at its fastest (0.4 m per tick, curves are 0.71 m).
constexpr int maxHops = 4;

Segment segmentOf(QcBlockPos cell, QcRails::Track track)
{
    const QcVec3 from = QcRails::endPoint(cell, track.ends[0]);
    const QcVec3 to = QcRails::endPoint(cell, track.ends[1]);
    const QcVec3 delta = to - from;
    return {cell, track, from, to, delta.normalized(), delta.length()};
}

// Where along the segment the point lies, from its first end, kept on the segment.
double alongOf(const Segment &segment, const QcVec3 &point)
{
    return std::clamp((point - segment.from).dot(segment.direction), 0.0, segment.length);
}

QcVec3 pointAt(const Segment &segment, double along)
{
    return segment.from + segment.direction * along;
}

// The track that goes on from end `end` of `segment`, and which of its ends meets it.
struct Continuation
{
    Segment segment;
    int entry = 0;
};

std::optional<Continuation> continuation(const Segment &segment, int end, const QcRails::Table &rails, const QcBlockAccess &world)
{
    const std::optional<QcRails::Joint> joint = QcRails::joined(world, rails, segment.cell, segment.track, end);
    if (!joint) {
        return std::nullopt;
    }
    return Continuation{segmentOf(joint->cell, joint->track), joint->entry};
}

// The yaw of `direction` among those that look the same on a cart (it is the same front and back),
// the one nearest to `previous`, so the cart turns the short way.
float nearestYaw(const QcVec3 &direction, float previous)
{
    const float yaw = yawAlong(direction);
    const float turned = qcNormalizedAngle(yaw + 180.0f);
    const auto distance = [previous](float candidate) {
        const float difference = std::abs(qcNormalizedAngle(candidate - previous + 180.0f) - 180.0f);
        return difference;
    };
    return distance(yaw) <= distance(turned) ? yaw : turned;
}

// Off the rails: falls, slides and stops like a body without a will of its own.
void freeStep(QcMinecart &cart, const QcBlockAccess &world, double dt)
{
    cart.motion.inWater = QcPhysics::isLiquidAt(world, QcPhysics::blockContaining(cart.motion.position));
    QcPhysics::stepEntity(cart.motion, {}, {QcMinecartBody::width, QcMinecartBody::height}, world, dt);
}

// Whether something solid stands beyond end `end` of the segment, level with the track.
bool blockedBeyond(const Segment &segment, int end, const QcBlockAccess &world)
{
    const QcBlockPos beyond = qcNeighbour(segment.cell, segment.track.ends[std::size_t(end)].side);
    return world.blockAt(beyond) && !QcPhysics::collisionBoxesAt(world, beyond).isEmpty();
}

// Whether the cells beyond end `end` of the segment lie in a chunk that is not loaded.
bool beyondUnloaded(const Segment &segment, int end, const QcBlockAccess &world)
{
    return !world.blockAt(qcNeighbour(segment.cell, segment.track.ends[std::size_t(end)].side));
}

// What a powered rail does to the speed: an unpowered one brakes, a powered one speeds up a rolling
// cart or starts a standing one away from a block at an end of its track.
double boosted(double speed, const Segment &segment, const QcRails::Table &rails, const QcBlockAccess &world, double dt)
{
    const QcRails::Behaviour rail = rails.behaviourOf(world.blockAt(segment.cell).value_or(qcAirBlockId));
    if (rail.action != QcRailAction::Boost) {
        return speed;
    }
    if (!rail.powered) {
        return std::abs(speed) < brakeStop ? 0.0 : speed * 0.5;
    }
    if (speed != 0.0) {
        return speed + (speed > 0.0 ? 1.0 : -1.0) * boostAcceleration * dt;
    }
    // Blocked at both ends or at none: it stays (no direction decides).
    const bool front = blockedBeyond(segment, 1, world);
    const bool back = blockedBeyond(segment, 0, world);
    return front == back ? 0.0 : front ? -boostKick : boostKick;
}

// The speed along the track after gravity on a slope, drag, powered rails and the speed limit.
double nextSpeed(const QcMinecart &cart, const Segment &segment, const QcRails::Table &rails, const QcBlockAccess &world, double dt)
{
    double speed = cart.motion.velocity.dot(segment.direction);
    if (segment.direction.y != 0.0) {
        speed -= slopeAcceleration * dt * (segment.direction.y > 0.0 ? 1.0 : -1.0);
    }
    speed *= cart.rider != 0 ? riddenDrag : emptyDrag;
    speed = boosted(speed, segment, rails, world, dt);
    speed = std::clamp(speed, -maxSpeed, maxSpeed);
    if (segment.direction.y == 0.0 && std::abs(speed) < restSpeed) {
        speed = 0.0;
    }
    return speed;
}

// Notes the activator rail of `segment`, if it is one.
void noteActivator(Result &result, const Segment &segment, const QcRails::Table &rails, const QcBlockAccess &world)
{
    const QcRails::Behaviour rail = rails.behaviourOf(world.blockAt(segment.cell).value_or(qcAirBlockId));
    if (rail.action == QcRailAction::Activate) {
        (rail.powered ? result.activated : result.deactivated) = true;
    }
}

// Where a cart is on its way through a tick: the track, how far along it and its speed along it.
struct Run
{
    Segment segment;
    double along = 0.0;
    double speed = 0.0;
};

// The cart reached end `forward` of its track and no rail goes on there: it waits with its speed
// while the chunk beyond is not loaded (the track may go on there), else it stops just inside the
// last rail.
void reachEnd(Run &run, bool forward, Result &result, const QcBlockAccess &world)
{
    run.along = forward ? run.segment.length - endInset : endInset;
    if (beyondUnloaded(run.segment, forward ? 1 : 0, world)) {
        result.waiting = true;
        return;
    }
    run.speed = 0.0;
    result.stoppedAtEnd = true;
    result.heldAtEnd = forward ? run.segment.direction.y < 0.0 : run.segment.direction.y > 0.0;
}

// Moves the cart `distance` metres along the track and on into the rails that join it.
void roll(Run &run, double distance, Result &result, const QcRails::Table &rails, const QcBlockAccess &world)
{
    for (int hop = 0; hop < maxHops && distance > 0.0; ++hop) {
        const bool forward = run.speed >= 0.0;
        const double room = forward ? run.segment.length - run.along : run.along;
        if (distance <= room) {
            run.along += forward ? distance : -distance;
            return;
        }

        distance -= room;
        const std::optional<Continuation> next = continuation(run.segment, forward ? 1 : 0, rails, world);
        if (!next) {
            reachEnd(run, forward, result, world);
            return;
        }
        run.segment = next->segment;
        noteActivator(result, run.segment, rails, world);
        run.along = next->entry == 0 ? 0.0 : run.segment.length;
        run.speed = next->entry == 0 ? std::abs(run.speed) : -std::abs(run.speed);
    }
}

} // namespace

float yawAlong(const QcVec3 &direction)
{
    return qcNormalizedAngle(float(qRadiansToDegrees(std::atan2(direction.x, -direction.z))));
}

std::optional<Segment> segmentAt(const QcVec3 &feet, const QcRails::Table &rails, const QcBlockAccess &world)
{
    // Feet on the border of two cells (the end of a track) belong to the rails on both sides.
    constexpr double border = 1e-9;
    const QcBlockPos base = QcPhysics::blockContaining(feet);
    const bool onX = feet.x == std::floor(feet.x);
    const bool onZ = feet.z == std::floor(feet.z);
    const std::array<QcBlockPos, 4> columns{base, base.offset(onX ? -1 : 0, 0, 0), base.offset(0, 0, onZ ? -1 : 0),
                                            QcPhysics::blockContaining(feet - QcVec3{onX ? border : 0.0, 0.0, onZ ? border : 0.0})};

    std::optional<Segment> nearest;
    double nearestDistance = snapDistance;
    for (const QcBlockPos column : columns) {
        for (const QcBlockPos cell : {column, column.offset(0, -1, 0), column.offset(0, 1, 0)}) {
            const std::optional<QcBlockId> block = world.blockAt(cell);
            const std::optional<QcRails::Track> &track = block ? rails.trackOf(*block) : std::nullopt;
            if (!track) {
                continue;
            }
            const Segment segment = segmentOf(cell, *track);
            const double distance = (pointAt(segment, alongOf(segment, feet)) - feet).length();
            if (distance < nearestDistance || (!nearest && distance <= snapDistance)) {
                nearest = segment;
                nearestDistance = distance;
            }
        }
    }
    return nearest;
}

Result step(QcMinecart &cart, const QcRails::Table &rails, const QcBlockAccess &world, double dt)
{
    const std::optional<Segment> start = segmentAt(cart.motion.position, rails, world);
    if (!start) {
        freeStep(cart, world, dt);
        return {};
    }

    Run run{*start, alongOf(*start, cart.motion.position), nextSpeed(cart, *start, rails, world, dt)};
    Result result;
    result.onRail = true;
    noteActivator(result, run.segment, rails, world);
    roll(run, std::abs(run.speed) * dt, result, rails, world);

    cart.motion.position = pointAt(run.segment, run.along);
    cart.motion.velocity = run.segment.direction * run.speed;
    cart.motion.onGround = true;
    cart.motion.inWater = false;
    cart.yaw = nearestYaw(run.segment.direction, cart.yaw);
    result.segment = run.segment;
    return result;
}

} // namespace QcMinecartMotion
