#include "server/projectiles/QcProjectileFlight.h"

#include "common/physics/QcPhysics.h"
#include "common/physics/QcRaycast.h"

#include <QtMath>

#include <cmath>
#include <optional>

namespace {

// Whether `box` lies in the box the segment sweeps (a cheap test before the exact one).
bool nearSegment(const QcAabb &box, const QcAabb &swept)
{
    return box.intersects(swept);
}

// The body the segment meets first, closer than `limit`; its distance along the segment.
std::optional<std::pair<double, quint32>> firstBody(const QcProjectile &projectile, const QList<QcProjectileFlight::Body> &bodies,
                                                    const QcVec3 &direction, double limit, double grow)
{
    const QcVec3 from = projectile.motion.position;
    const QcAabb swept = QcAabb{from, from}.expandedTowards(direction * limit);
    const QcVec3 margin{grow, grow, grow};
    const bool shooterImmune = projectile.age < QcProjectileFlight::shooterGraceSeconds;

    std::optional<std::pair<double, quint32>> first;
    for (const QcProjectileFlight::Body &body : bodies) {
        if (shooterImmune && body.id == projectile.shooter) {
            continue;
        }

        const QcAabb box{body.box.min - margin, body.box.max + margin};
        if (!nearSegment(box, swept)) {
            continue;
        }

        const std::optional<double> distance = QcRaycast::intersectBox(from, direction, box, limit);
        if (distance && (!first || *distance < first->first)) {
            first = std::pair{*distance, body.id};
        }
    }
    return first;
}

// How far along the segment from `from` in `direction` a floating projectile first enters a liquid,
// sampled every waterProbe metres up to `limit`.
std::optional<double> firstWater(const QcBlockAccess &world, const QcVec3 &from, const QcVec3 &direction, double limit)
{
    constexpr double waterProbe = 0.25;
    for (int step = 1; step * waterProbe < limit; ++step) {
        const double along = step * waterProbe;
        if (QcPhysics::isLiquidAt(world, QcPhysics::blockContaining(from + direction * along))) {
            return along;
        }
    }
    return std::nullopt;
}

// A floating projectile in water (decision E-503): it drifts on with the water's drag, never into a
// cell without water, and rises to the surface of the uppermost water block, where it rests.
QcProjectileFlight::Step floatStep(const QcBlockAccess &world, QcProjectile &projectile, const QcProjectileDefinition &definition, double dt)
{
    QcEntityMotion &motion = projectile.motion;
    const double keep = std::exp(-definition.waterDrag * dt);
    QcVec3 velocity{motion.velocity.x * keep, 0.0, motion.velocity.z * keep};
    QcVec3 next = motion.position + QcVec3{velocity.x, 0.0, velocity.z} * dt;
    if (!QcPhysics::isWaterAt(world, QcPhysics::blockContaining(next))) {
        velocity = {};
        next = motion.position;
    }

    const QcBlockPos cell = QcPhysics::blockContaining(next);
    const bool covered = QcPhysics::isWaterAt(world, cell.offset(0, 1, 0));
    const double surface = cell.y + QcProjectileFlight::floatHeight;
    const double risen = next.y + QcProjectileFlight::floatRise * dt;
    next.y = covered ? risen : std::min(risen, surface);
    velocity.y = next.y > motion.position.y ? QcProjectileFlight::floatRise : 0.0;
    motion.position = next;
    motion.velocity = velocity;
    return {};
}

} // namespace

namespace QcProjectileFlight {

QcVec3 direction(double yawDegrees, double pitchDegrees)
{
    const double yaw = qDegreesToRadians(yawDegrees);
    const double pitch = qDegreesToRadians(pitchDegrees);
    return {std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
}

void turnAlong(QcProjectile &projectile)
{
    const QcVec3 v = projectile.motion.velocity;
    const double horizontal = std::hypot(v.x, v.z);
    if (horizontal <= 0.0 && v.y == 0.0) {
        return;
    }
    projectile.yaw = float(qRadiansToDegrees(std::atan2(v.x, -v.z)));
    projectile.pitch = float(qRadiansToDegrees(std::atan2(v.y, horizontal)));
}

namespace {

// A step that ends the flight's motion this tick without a block hit: `outcome`, and the body hit.
Step ended(Outcome outcome, quint32 body = 0)
{
    Step step;
    step.outcome = outcome;
    step.body = body;
    return step;
}

} // namespace

Step step(const QcBlockAccess &world, QcProjectile &projectile, const QcProjectileDefinition &definition, const QList<Body> &bodies,
          double dt)
{
    QcEntityMotion &motion = projectile.motion;
    const QcBlockPos here = QcPhysics::blockContaining(motion.position);
    const bool inLiquid = QcPhysics::isLiquidAt(world, here);
    motion.inWater = QcPhysics::isWaterAt(world, here);
    if (definition.floats && inLiquid) {
        // Water carries it; a hot liquid (lava) burns it up.
        return motion.inWater ? floatStep(world, projectile, definition, dt) : ended(Outcome::Burnt);
    }
    QcVec3 velocity = motion.velocity;
    velocity.y -= definition.gravity * dt;
    velocity = velocity * std::exp(-(inLiquid ? definition.waterDrag : definition.drag) * dt);

    const QcVec3 delta = velocity * dt;
    const double length = delta.length();
    const QcVec3 end = motion.position + delta;
    if (length > 0.0 && !world.blockAt(QcPhysics::blockContaining(end))) {
        // Waiting at an unloaded chunk keeps velocity and orientation as they were.
        return ended(Outcome::Waiting);
    }

    motion.velocity = velocity;
    turnAlong(projectile);
    if (length <= 0.0) {
        return {};
    }

    const QcVec3 direction = delta * (1.0 / length);
    const std::optional<QcRaycastHit> block = QcRaycast::cast(world, motion.position, direction, length, QcRaycast::Targets::Solid);
    const double blockDistance = block ? block->distance : length;
    if (const auto body = firstBody(projectile, bodies, direction, blockDistance, definition.size * 0.5)) {
        motion.position = motion.position + direction * body->first;
        return ended(Outcome::HitBody, body->second);
    }

    if (definition.floats) {
        if (const std::optional<double> water = firstWater(world, motion.position, direction, blockDistance)) {
            // A fast floater crossing shallow water lands in it instead of in the floor below.
            motion.position = motion.position + direction * *water;
            return {};
        }
    }

    if (block) {
        // The centre stops so far before the block that the tip reaches stickDepth into it.
        const double back = std::max(0.0, definition.size * 0.5 - stickDepth);
        motion.position = block->point - direction * back;
        return {Outcome::HitBlock, 0, block->block, block->face, block->point};
    }

    motion.position = end;
    return {};
}

double damageAt(const QcProjectileDefinition &definition, double speed, double factor)
{
    return std::ceil(definition.damage * speed / QcProjectileDefinition::damageSpeed * factor);
}

} // namespace QcProjectileFlight
