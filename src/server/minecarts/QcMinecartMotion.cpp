// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/minecarts/QcMinecartMotion.h"

#include "common/gameplay/QcStandPose.h"
#include "common/physics/QcPhysics.h"
#include "common/world/QcMinecartBody.h"

#include <QtMath>

#include <algorithm>
#include <array>
#include <cmath>

namespace QcMinecartMotion {

namespace {

// Share of its horizontal speed a cart off the rails loses per tick in the air.
constexpr double flightDrag = 0.02;

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

// Whether the ground carries the middle of the cart: a thin column under its centre cannot move down a
// little. A cart whose middle is beyond an edge (just off the end of a track) tips over without
// braking on the edge; one on a block taller than a cell (a fence, a wall) stands on it; one just
// loaded (onGround is not saved) knows at once that it lies on the ground.
bool carriedAtCentre(const QcVec3 &feet, const QcBlockAccess &world)
{
    constexpr double probe = 0.01;
    constexpr double columnWidth = 0.02;
    const QcAabb column = QcPhysics::boxAt(feet, columnWidth, columnWidth);
    return QcPhysics::moveBox(column, {0.0, -probe, 0.0}, world).applied.y > -probe * 0.5;
}

// Whether the columns of cells the cart would move through in this tick lie (partly) in a chunk that
// is not loaded (chunks hold whole columns).
bool headsIntoUnloaded(const QcMinecart &cart, const QcBlockAccess &world, double dt)
{
    const QcAabb from = QcPhysics::boxAt(cart.motion.position, QcMinecartBody::width, QcMinecartBody::height);
    const QcVec3 way = cart.motion.velocity * dt;
    const QcBlockPos first = QcPhysics::blockContaining({std::min(from.min.x, from.min.x + way.x), 0.0, std::min(from.min.z, from.min.z + way.z)});
    const QcBlockPos last = QcPhysics::blockContaining({std::max(from.max.x, from.max.x + way.x), 0.0, std::max(from.max.z, from.max.z + way.z)});
    const int y = QcPhysics::blockContaining(cart.motion.position).y;
    for (int x = first.x; x <= last.x; ++x) {
        for (int z = first.z; z <= last.z; ++z) {
            if (!world.blockAt({x, y, z})) {
                return true;
            }
        }
    }
    return false;
}

// Off the rails: falls and slides like a body without a will of its own (decision E-430). In the air
// it keeps its momentum (flying off the end of a track): steering towards a slightly slower velocity
// in its direction makes QcEntityMotion::step's air control act as weak drag. On the ground it stops within a
// few ticks where the ground grips and coasts far on slippery ground, and stands still below
// `restSpeed`. Heading into a chunk that is not loaded, it waits there with its speed, as at the end
// of a track.
[[nodiscard]] Result freeStep(QcMinecart &cart, const QcBlockAccess &world, double dt)
{
    Result result;
    if (headsIntoUnloaded(cart, world, dt)) {
        result.waiting = true;
        return result;
    }

    cart.motion.inWater = QcPhysics::isLiquidAt(world, QcPhysics::blockContaining(cart.motion.position));
    QcPhysics::Steering coasting;
    coasting.slideDrag = QcPhysics::coastDrag;
    if (cart.motion.velocity.y <= 0.0) {
        cart.motion.onGround = carriedAtCentre(cart.motion.position, world);
    }
    double horizontal = std::hypot(cart.motion.velocity.x, cart.motion.velocity.z);
    if (cart.motion.onGround && horizontal < restSpeed) {
        cart.motion.velocity.x = 0.0;
        cart.motion.velocity.z = 0.0;
        horizontal = 0.0;
    }
    if (!cart.motion.onGround && !cart.motion.inWater && horizontal > 0.0) {
        const double blend = std::min(1.0, QcPhysics::airAcceleration * dt);
        coasting.wish = {cart.motion.velocity.x / horizontal, 0.0, cart.motion.velocity.z / horizontal};
        coasting.speed = horizontal * std::max(0.0, 1.0 - flightDrag * dt * QcPhysics::tickRate / blend);
    }
    cart.motion.step(coasting, {QcMinecartBody::width, QcMinecartBody::height}, world, dt);
    return result;
}

// Whether a block level with end `end` of the segment (where the cart would go on, one higher at a
// raised end) stands in the cart's way: a buffer stop, also for a powered rail starting a cart.
bool blockedBeyond(const Segment &segment, int end, const QcBlockAccess &world)
{
    const QcRails::End &at = segment.track.ends[static_cast<std::size_t>(end)];
    const QcBlockPos beyond = qcNeighbour(segment.cell, at.side).offset(0, at.raised ? 1 : 0, 0);
    return world.blockAt(beyond) && !QcPhysics::collisionBoxesAt(world, beyond).isEmpty();
}

// Whether the cells beyond end `end` of the segment lie in a chunk that is not loaded.
bool beyondUnloaded(const Segment &segment, int end, const QcBlockAccess &world)
{
    return !world.blockAt(qcNeighbour(segment.cell, segment.track.ends[static_cast<std::size_t>(end)].side));
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

// Where a cart is on its way through a tick: the track, how far along it and its speed along it;
// after it left the track, how much of the tick's way it had left.
struct Run
{
    Segment segment;
    double along = 0.0;
    double speed = 0.0;
    double overshoot = 0.0;
};

// One cart's way along the rails through a tick: where it is and what happened on the way.
class Ride
{
public:
    // Starts on `run` and notes the activator rail it starts on.
    Ride(const Run &run, const QcRails::Table &rails, const QcBlockAccess &world)
        : m_run(run)
        , m_rails(rails)
        , m_world(world)
    {
        m_result.onRail = true;
        noteActivator();
    }

    // Moves the cart `distance` metres along the track and on into the rails that join it.
    void roll(double distance)
    {
        for (int hop = 0; hop < maxHops && distance > 0.0; ++hop) {
            const bool forward = m_run.speed >= 0.0;
            const double room = forward ? m_run.segment.length - m_run.along : m_run.along;
            if (distance <= room) {
                m_run.along += forward ? distance : -distance;
                return;
            }

            distance -= room;
            const std::optional<Continuation> next = continuation(m_run.segment, forward ? 1 : 0, m_rails, m_world);
            if (!next) {
                m_run.overshoot = distance;
                reachEnd(forward);
                return;
            }
            m_run.segment = next->segment;
            noteActivator();
            m_run.along = next->entry == 0 ? 0.0 : m_run.segment.length;
            m_run.speed = next->entry == 0 ? std::abs(m_run.speed) : -std::abs(m_run.speed);
        }
    }

    [[nodiscard]] const Run &run() const { return m_run; }
    [[nodiscard]] const Result &result() const { return m_result; }

private:
    // Notes the activator rail of the segment it is on, if it is one.
    void noteActivator()
    {
        const QcRails::Behaviour rail = m_rails.behaviourOf(m_world.blockAt(m_run.segment.cell).value_or(qcAirBlockId));
        if (rail.action == QcRailAction::Activate) {
            (rail.powered ? m_result.activated : m_result.deactivated) = true;
        }
    }

    // The cart reached end `forward` of its track and no rail goes on there: it waits with its speed
    // while the chunk beyond is not loaded (the track may go on there), it stops just inside the last
    // rail before a block level with the end, and else it leaves the track with its speed (decision
    // E-430).
    void reachEnd(bool forward)
    {
        const int end = forward ? 1 : 0;
        if (!beyondUnloaded(m_run.segment, end, m_world) && !blockedBeyond(m_run.segment, end, m_world)) {
            m_run.along = forward ? m_run.segment.length : 0.0;
            m_result.derailed = true;
            return;
        }

        m_run.along = forward ? m_run.segment.length - endInset : endInset;
        if (beyondUnloaded(m_run.segment, end, m_world)) {
            m_result.waiting = true;
            return;
        }
        m_run.speed = 0.0;
        m_result.stoppedAtEnd = true;
        m_result.heldAtEnd = forward ? m_run.segment.direction.y < 0.0 : m_run.segment.direction.y > 0.0;
    }

    Run m_run;
    Result m_result;
    const QcRails::Table &m_rails;
    const QcBlockAccess &m_world;
};

// The velocity of a cart leaving its track at end `end`: horizontally straight out of the end (off a
// curve along the side of that end, not diagonally) with the horizontal part of its speed along the
// track, and up or down as the track ran. Off the top of a slope (45 degrees) it flies up, horizontally
// and upwards each with cos 45 degrees (0.71) of the speed it rolled with; off a level track it keeps
// all of it horizontally.
QcVec3 exitVelocity(const Run &run, int end)
{
    const QcVec3 velocity = run.segment.direction * run.speed;
    const QcFaceOffset out = qcFaceOffset(run.segment.track.ends[static_cast<std::size_t>(end)].side);
    const double horizontal = std::hypot(velocity.x, velocity.z);
    return {out.dx * horizontal, velocity.y, out.dz * horizontal};
}

// Puts a cart that left its track at the end and moves it on by the rest of the tick's way (at least
// a little, so that it is beyond the rail's cell and no longer on it).
void leaveTrack(QcMinecart &cart, const Run &run, const QcBlockAccess &world)
{
    constexpr double clearance = 1e-3;
    const int end = run.speed >= 0.0 ? 1 : 0;
    const QcVec3 velocity = exitVelocity(run, end);
    const QcVec3 start = pointAt(run.segment, run.along);
    const QcAabb box = QcPhysics::boxAt(start, QcMinecartBody::width, QcMinecartBody::height);
    const QcVec3 way = velocity.normalized() * (run.overshoot + clearance);

    cart.motion.position = start + QcPhysics::moveBox(box, way, world).applied;
    cart.motion.velocity = velocity;
    cart.yaw = nearestYaw({velocity.x, 0.0, velocity.z}, cart.yaw);
    cart.motion.onGround = false;
    cart.motion.inWater = false;
}

} // namespace

float yawAlong(const QcVec3 &direction)
{
    return qcNormalizedAngle(static_cast<float>(qRadiansToDegrees(std::atan2(direction.x, -direction.z))));
}

std::optional<Segment> segmentAt(const QcVec3 &feet, const QcRails::Table &rails, const QcBlockAccess &world, bool fromAbove)
{
    // A point on the track this much higher than the feet still counts as level (rounding).
    constexpr double level = 1e-6;
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
            const QcVec3 nearestPoint = pointAt(segment, alongOf(segment, feet));
            if (fromAbove && nearestPoint.y > feet.y + level) {
                continue;
            }
            const double distance = (nearestPoint - feet).length();
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
    // In the air it gets on a track only from above or level with it, not up onto one above it.
    const std::optional<Segment> start = segmentAt(cart.motion.position, rails, world, !cart.motion.onGround);
    if (!start) {
        return freeStep(cart, world, dt);
    }

    const double speed = nextSpeed(cart, *start, rails, world, dt);
    Ride ride(Run{*start, alongOf(*start, cart.motion.position), speed}, rails, world);
    ride.roll(std::abs(speed) * dt);
    const Run &run = ride.run();
    Result result = ride.result();
    if (result.derailed) {
        leaveTrack(cart, run, world);
        result.onRail = false;
        return result;
    }

    cart.yaw = nearestYaw(run.segment.direction, cart.yaw);
    cart.motion.position = pointAt(run.segment, run.along);
    cart.motion.velocity = run.segment.direction * run.speed;
    cart.motion.onGround = true;
    cart.motion.inWater = false;
    result.segment = run.segment;
    return result;
}

} // namespace QcMinecartMotion
