#include "QcPhysics.h"

#include "QcContentRegistry.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace QcPhysics {

QcBlockPos blockContaining(const QcVec3 &point)
{
    return {int(std::floor(point.x)), int(std::floor(point.y)), int(std::floor(point.z))};
}

namespace {

QList<QcAabb> solidBoxesIn(const QcAabb &region, const QcBlockAccess &world)
{
    QList<QcAabb> boxes;
    // A small epsilon keeps a box resting exactly on a face from picking up the next block layer.
    constexpr double epsilon = 1e-7;
    const int x0 = int(std::floor(region.min.x + epsilon));
    const int y0 = int(std::floor(region.min.y + epsilon));
    const int z0 = int(std::floor(region.min.z + epsilon));
    const int x1 = int(std::floor(region.max.x - epsilon));
    const int y1 = int(std::floor(region.max.y - epsilon));
    const int z1 = int(std::floor(region.max.z - epsilon));
    for (int x = x0 - 1; x <= x1 + 1; ++x) {
        for (int y = y0 - 1; y <= y1 + 1; ++y) {
            for (int z = z0 - 1; z <= z1 + 1; ++z) {
                if (isSolidAt(world, {x, y, z})) {
                    boxes.append(QcAabb{{double(x), double(y), double(z)}, {x + 1.0, y + 1.0, z + 1.0}});
                }
            }
        }
    }
    return boxes;
}

} // namespace

QcAabb boxAt(const QcVec3 &feet, double width, double height)
{
    const double half = width * 0.5;
    return {{feet.x - half, feet.y, feet.z - half}, {feet.x + half, feet.y + height, feet.z + half}};
}

bool isSolidAt(const QcBlockAccess &world, QcBlockPos pos)
{
    if (pos.y < 0) {
        return true;
    }
    const std::optional<QcBlockId> block = world.blockAt(pos);
    if (!block) {
        return true;
    }
    const QcContentRegistry &content = world.content();
    return content.isValidBlock(*block) && content.block(*block).solid;
}

bool isLiquidAt(const QcBlockAccess &world, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    const QcContentRegistry &content = world.content();
    return block && content.isValidBlock(*block) && content.block(*block).liquid;
}

MoveResult moveBox(const QcAabb &box, const QcVec3 &delta, const QcBlockAccess &world)
{
    const QList<QcAabb> obstacles = solidBoxesIn(box.expandedTowards(delta), world);
    MoveResult result;
    QcAabb moving = box;

    double dy = delta.y;
    for (const QcAabb &obstacle : obstacles) {
        dy = moving.clipY(obstacle, dy);
    }
    moving = moving.moved({0.0, dy, 0.0});

    double dx = delta.x;
    for (const QcAabb &obstacle : obstacles) {
        dx = moving.clipX(obstacle, dx);
    }
    moving = moving.moved({dx, 0.0, 0.0});

    double dz = delta.z;
    for (const QcAabb &obstacle : obstacles) {
        dz = moving.clipZ(obstacle, dz);
    }

    result.applied = {dx, dy, dz};
    result.blockedX = dx != delta.x;
    result.blockedY = dy != delta.y;
    result.blockedZ = dz != delta.z;
    return result;
}

MoveResult stepEntity(QcEntityMotion &motion, const QcVec3 &wish, double speed, bool jump, double width, double height,
                      const QcBlockAccess &world, double dt)
{
    const double acceleration = motion.inWater ? waterAcceleration : (motion.onGround ? groundAcceleration : airAcceleration);
    const double blend = std::min(1.0, acceleration * dt);
    motion.velocity.x += (wish.x * speed - motion.velocity.x) * blend;
    motion.velocity.z += (wish.z * speed - motion.velocity.z) * blend;

    if (motion.inWater) {
        motion.velocity.y -= waterGravity * dt;
        if (jump) {
            motion.velocity.y = std::min(motion.velocity.y + swimUpAcceleration * dt, swimUpSpeed);
        }
        motion.velocity.y = std::max(motion.velocity.y, -waterSinkSpeed);
    } else {
        if (jump && motion.onGround) {
            motion.velocity.y = jumpVelocity;
        }
        motion.velocity.y = std::max(motion.velocity.y - gravity * dt, -terminalVelocity);
    }

    const MoveResult move = moveBox(boxAt(motion.position, width, height), motion.velocity * dt, world);
    motion.position += move.applied;
    motion.onGround = move.blockedY && motion.velocity.y < 0.0;
    if (move.blockedY) {
        motion.velocity.y = 0.0;
    }
    if (move.blockedX) {
        motion.velocity.x = 0.0;
    }
    if (move.blockedZ) {
        motion.velocity.z = 0.0;
    }
    return move;
}

void stepPlayer(QcEntityMotion &motion, const QcPlayerInput &input, const QcBlockAccess &world, double dt)
{
    motion.inWater = isLiquidAt(world, blockContaining(motion.position + QcVec3{0.0, 0.4, 0.0}));

    const double yaw = qDegreesToRadians(double(input.yaw));
    const QcVec3 forward{std::sin(yaw), 0.0, -std::cos(yaw)};
    const QcVec3 right{std::cos(yaw), 0.0, std::sin(yaw)};
    QcVec3 wish = forward * std::clamp(double(input.forward), -1.0, 1.0) + right * std::clamp(double(input.strafe), -1.0, 1.0);
    if (wish.length() > 1.0) {
        wish = wish.normalized();
    }

    double speed = walkSpeed;
    if (motion.inWater) {
        speed = swimSpeed;
    } else if (input.sneak) {
        speed = sneakSpeed;
    } else if (input.sprint && input.forward > 0.0f) {
        speed = sprintSpeed;
    }
    stepEntity(motion, wish, speed, input.jump, playerWidth, playerHeight, world, dt);
}

} // namespace QcPhysics
