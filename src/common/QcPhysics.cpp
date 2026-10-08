#include "QcPhysics.h"

#include "QcContentRegistry.h"

#include <QVarLengthArray>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace QcPhysics {

QcBlockPos blockContaining(const QcVec3 &point)
{
    return {int(std::floor(point.x)), int(std::floor(point.y)), int(std::floor(point.z))};
}

namespace {

QcAabb cellBox(QcBlockPos pos)
{
    return {{double(pos.x), double(pos.y), double(pos.z)}, {pos.x + 1.0, pos.y + 1.0, pos.z + 1.0}};
}

template<typename Boxes>
void appendCollisionBoxes(const QcBlockAccess &world, QcBlockPos pos, Boxes &boxes)
{
    if (pos.y < 0) {
        boxes.append(cellBox(pos));
        return;
    }
    const std::optional<QcBlockId> block = world.blockAt(pos);
    if (!block) {
        boxes.append(cellBox(pos));
        return;
    }
    const QcContentRegistry &content = world.content();
    if (!content.isValidBlock(*block)) {
        return;
    }
    const QcVec3 origin{double(pos.x), double(pos.y), double(pos.z)};
    for (const QcAabb &box : content.state(*block).collision) {
        boxes.append(box.moved(origin));
    }
}

// Collision boxes near a moving entity; enough inline room for the cells around a mob or player.
using ObstacleList = QVarLengthArray<QcAabb, 96>;

ObstacleList solidBoxesIn(const QcAabb &region, const QcBlockAccess &world)
{
    ObstacleList boxes;
    // A small epsilon keeps a box resting exactly on a face from picking up the next block layer.
    constexpr double epsilon = 1e-7;
    const int x0 = int(std::floor(region.min.x + epsilon));
    const int y0 = int(std::floor(region.min.y + epsilon));
    const int z0 = int(std::floor(region.min.z + epsilon));
    const int x1 = int(std::floor(region.max.x - epsilon));
    const int y1 = int(std::floor(region.max.y - epsilon));
    const int z1 = int(std::floor(region.max.z - epsilon));
    // One block of margin: collision boxes reach up to 1.5 m into the cell above (fences).
    for (int x = x0 - 1; x <= x1 + 1; ++x) {
        for (int y = y0 - 1; y <= y1 + 1; ++y) {
            for (int z = z0 - 1; z <= z1 + 1; ++z) {
                appendCollisionBoxes(world, {x, y, z}, boxes);
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

QList<QcAabb> collisionBoxesAt(const QcBlockAccess &world, QcBlockPos pos)
{
    QList<QcAabb> boxes;
    appendCollisionBoxes(world, pos, boxes);
    return boxes;
}

bool touchesClimbable(const QcBlockAccess &world, const QcAabb &box)
{
    constexpr double inset = 1e-3;
    const QcContentRegistry &content = world.content();
    for (int x = int(std::floor(box.min.x + inset)); x <= int(std::floor(box.max.x - inset)); ++x) {
        for (int y = int(std::floor(box.min.y + inset)); y <= int(std::floor(box.max.y - inset)); ++y) {
            for (int z = int(std::floor(box.min.z + inset)); z <= int(std::floor(box.max.z - inset)); ++z) {
                const std::optional<QcBlockId> block = world.blockAt({x, y, z});
                if (block && content.isValidBlock(*block) && content.block(*block).climbable) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool isLiquidAt(const QcBlockAccess &world, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    const QcContentRegistry &content = world.content();
    return block && content.isValidBlock(*block) && content.block(*block).liquid;
}

MoveResult moveBox(const QcAabb &box, const QcVec3 &delta, const QcBlockAccess &world)
{
    const ObstacleList obstacles = solidBoxesIn(box.expandedTowards(delta), world);
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
                      const QcBlockAccess &world, double dt, bool hold)
{
    motion.climbing = !motion.inWater && touchesClimbable(world, boxAt(motion.position, width, height));
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
        if (motion.climbing) {
            motion.velocity.y = std::max(motion.velocity.y, hold ? 0.0 : -climbDownSpeed);
            // Jumping or walking into the wall behind the ladder climbs it.
            const bool pushing = wish.length() > 0.0
                                 && [&] {
                                        const MoveResult probe = moveBox(boxAt(motion.position, width, height),
                                                                         {wish.x * 0.05, 0.0, wish.z * 0.05}, world);
                                        return probe.blockedX || probe.blockedZ;
                                    }();
            if (jump || pushing) {
                motion.velocity.y = climbSpeed;
            }
        }
    }

    MoveResult move = moveBox(boxAt(motion.position, width, height), motion.velocity * dt, world);
    if (motion.onGround && !motion.inWater && (move.blockedX || move.blockedZ)) {
        // Walking into something low (a slab, a stair step): try the same move lifted by up to
        // stepHeight and keep it when it gets further.
        const QcAabb start = boxAt(motion.position, width, height);
        const MoveResult up = moveBox(start, {0.0, stepHeight, 0.0}, world);
        const QcAabb raised = start.moved(up.applied);
        const MoveResult across = moveBox(raised, {motion.velocity.x * dt, 0.0, motion.velocity.z * dt}, world);
        const QcAabb moved = raised.moved(across.applied);
        const MoveResult down = moveBox(moved, {0.0, -up.applied.y, 0.0}, world);
        const double plain = std::hypot(move.applied.x, move.applied.z);
        const double stepped = std::hypot(across.applied.x, across.applied.z);
        if (stepped > plain && up.applied.y + down.applied.y > 0.0) {
            move.applied = up.applied + across.applied + down.applied;
            move.blockedX = across.blockedX;
            move.blockedZ = across.blockedZ;
            move.blockedY = true;
            motion.velocity.y = std::min(motion.velocity.y, 0.0);
        }
    }
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
    stepEntity(motion, wish, speed, input.jump, playerWidth, playerHeight, world, dt, input.sneak);
}

} // namespace QcPhysics
