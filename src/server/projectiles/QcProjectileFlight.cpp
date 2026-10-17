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

Step step(const QcBlockAccess &world, QcProjectile &projectile, const QcProjectileDefinition &definition, const QList<Body> &bodies,
          double dt)
{
    QcEntityMotion &motion = projectile.motion;
    const bool inLiquid = QcPhysics::isLiquidAt(world, QcPhysics::blockContaining(motion.position));
    motion.inWater = inLiquid;
    QcVec3 velocity = motion.velocity;
    velocity.y -= definition.gravity * dt;
    velocity = velocity * std::exp(-(inLiquid ? definition.waterDrag : definition.drag) * dt);

    const QcVec3 delta = velocity * dt;
    const double length = delta.length();
    const QcVec3 end = motion.position + delta;
    if (length > 0.0 && !world.blockAt(QcPhysics::blockContaining(end))) {
        // Waiting at an unloaded chunk keeps velocity and orientation as they were.
        return {Outcome::Waiting, 0, {}};
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
        return {Outcome::HitBody, body->second, {}};
    }

    if (block) {
        // The centre stops so far before the block that the tip reaches stickDepth into it.
        const double back = std::max(0.0, definition.size * 0.5 - stickDepth);
        motion.position = block->point - direction * back;
        return {Outcome::HitBlock, 0, block->block};
    }

    motion.position = end;
    return {};
}

double damageAt(const QcProjectileDefinition &definition, double speed, double factor)
{
    return std::ceil(definition.damage * speed / QcProjectileDefinition::damageSpeed * factor);
}

} // namespace QcProjectileFlight
