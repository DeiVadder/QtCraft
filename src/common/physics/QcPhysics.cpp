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

namespace {

// Whether a cell from `start` to z = `lastZ` holds a climbable block.
bool climbableInRow(const QcBlockAccess &world, const QcContentRegistry &content, QcBlockPos start, int lastZ)
{
    for (int z = start.z; z <= lastZ; ++z) {
        const std::optional<QcBlockId> block = world.blockAt({start.x, start.y, z});
        if (block && content.isValidBlock(*block) && content.block(*block).climbable) {
            return true;
        }
    }
    return false;
}

} // namespace

bool touchesClimbable(const QcBlockAccess &world, const QcAabb &box)
{
    constexpr double inset = 1e-3;
    const QcContentRegistry &content = world.content();
    const int firstZ = int(std::floor(box.min.z + inset));
    const int lastZ = int(std::floor(box.max.z - inset));

    for (int x = int(std::floor(box.min.x + inset)); x <= int(std::floor(box.max.x - inset)); ++x) {
        for (int y = int(std::floor(box.min.y + inset)); y <= int(std::floor(box.max.y - inset)); ++y) {
            if (climbableInRow(world, content, {x, y, firstZ}, lastZ)) {
                return true;
            }
        }
    }
    return false;
}

namespace {

// Smallest `slowsInside` of the cells from `start` to z = `lastZ`, 1 without any.
double slowdownInRow(const QcBlockAccess &world, const QcContentRegistry &content, QcBlockPos start, int lastZ)
{
    double factor = 1.0;
    for (int z = start.z; z <= lastZ; ++z) {
        const std::optional<QcBlockId> block = world.blockAt({start.x, start.y, z});
        if (block && content.isValidBlock(*block)) {
            factor = std::min(factor, content.block(*block).slowsInside);
        }
    }
    return factor;
}

} // namespace

double slowdownInside(const QcBlockAccess &world, const QcAabb &box)
{
    constexpr double inset = 1e-3;
    const QcContentRegistry &content = world.content();
    const int firstZ = int(std::floor(box.min.z + inset));
    const int lastZ = int(std::floor(box.max.z - inset));

    double factor = 1.0;
    for (int x = int(std::floor(box.min.x + inset)); x <= int(std::floor(box.max.x - inset)); ++x) {
        for (int y = int(std::floor(box.min.y + inset)); y <= int(std::floor(box.max.y - inset)); ++y) {
            factor = std::min(factor, slowdownInRow(world, content, {x, y, firstZ}, lastZ));
        }
    }
    return factor;
}

SurfaceFactors surfaceUnder(const QcBlockAccess &world, const QcVec3 &feet)
{
    const std::optional<QcBlockId> block = world.blockAt(blockContaining(feet - QcVec3{0.0, 0.01, 0.0}));
    const QcContentRegistry &content = world.content();
    if (!block || !content.isValidBlock(*block)) {
        return {};
    }

    const QcBlockDefinition &definition = content.block(*block);
    return {definition.surfaceSpeed, definition.surfaceJump};
}

bool isLiquidAt(const QcBlockAccess &world, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    const QcContentRegistry &content = world.content();
    return block && content.isValidBlock(*block) && content.block(*block).liquid;
}

double liquidFactorAt(const QcBlockAccess &world, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    const QcContentRegistry &content = world.content();
    return block && content.isValidBlock(*block) && content.block(*block).liquid ? content.block(*block).flow.movementFactor : 1.0;
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

namespace {

void applyWaterVertical(QcEntityMotion &motion, bool jump, double factor, double dt)
{
    motion.velocity.y -= waterGravity * dt;
    if (jump) {
        motion.velocity.y = std::min(motion.velocity.y + swimUpAcceleration * dt, swimUpSpeed * factor);
    }
    motion.velocity.y = std::max(motion.velocity.y, -waterSinkSpeed * factor);
}

// Whether walking in the wished direction runs into a wall right away.
bool pushesIntoWall(const QcEntityMotion &motion, const QcVec3 &wish, BodySize size, const QcBlockAccess &world)
{
    const MoveResult probe = moveBox(boxAt(motion.position, size.width, size.height), {wish.x * 0.05, 0.0, wish.z * 0.05}, world);
    return probe.blockedX || probe.blockedZ;
}

void applyAirVertical(QcEntityMotion &motion, const Steering &steering, BodySize size, const QcBlockAccess &world, double dt)
{
    if (steering.jump && motion.onGround) {
        // Mud keeps a jump low (decision E-207).
        motion.velocity.y = jumpVelocity * surfaceUnder(world, motion.position).jump;
    }
    motion.velocity.y = std::max(motion.velocity.y - gravity * dt, -terminalVelocity);

    if (motion.climbing) {
        motion.velocity.y = std::max(motion.velocity.y, steering.hold ? 0.0 : -climbDownSpeed);
        // Jumping or walking into the wall behind the ladder climbs it.
        const bool pushing = steering.wish.length() > 0.0 && pushesIntoWall(motion, steering.wish, size, world);
        if (steering.jump || pushing) {
            motion.velocity.y = climbSpeed;
        }
    }
}

// Walking into something low (a slab, a stair step): tries the same move lifted by up to stepHeight
// and keeps it in `move` when it gets further.
void tryStepUp(MoveResult &move, QcEntityMotion &motion, BodySize size, const QcBlockAccess &world, double dt)
{
    const QcAabb start = boxAt(motion.position, size.width, size.height);
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

void applyMove(QcEntityMotion &motion, const MoveResult &move)
{
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
}

} // namespace

MoveResult stepEntity(QcEntityMotion &motion, const Steering &steering, BodySize size, const QcBlockAccess &world, double dt)
{
    const QcAabb body = boxAt(motion.position, size.width, size.height);
    motion.climbing = !motion.inWater && touchesClimbable(world, body);
    const double inside = slowdownInside(world, body);
    motion.slowed = inside < 1.0;
    const double acceleration = motion.inWater ? waterAcceleration : (motion.onGround ? groundAcceleration : airAcceleration);
    const double blend = std::min(1.0, acceleration * dt);
    // Thick liquids (lava) slow every movement inside them, cobwebs too, and mud slows walking on it.
    const double liquid = motion.inWater ? liquidFactorAt(world, blockContaining(motion.position + QcVec3{0.0, 0.4, 0.0})) : 1.0;
    const double surface = motion.onGround ? surfaceUnder(world, motion.position).speed : 1.0;
    const double factor = liquid * inside * surface;
    motion.velocity.x += (steering.wish.x * steering.speed * factor - motion.velocity.x) * blend;
    motion.velocity.z += (steering.wish.z * steering.speed * factor - motion.velocity.z) * blend;

    if (motion.inWater) {
        applyWaterVertical(motion, steering.jump, factor, dt);
    } else {
        applyAirVertical(motion, steering, size, world, dt);
    }
    if (motion.slowed) {
        // Sinks slowly through a cobweb and hardly jumps in it.
        motion.velocity.y *= inside;
    }

    MoveResult move = moveBox(boxAt(motion.position, size.width, size.height), motion.velocity * dt, world);
    // A body that just jumped keeps its jump instead of stepping up.
    if (motion.onGround && !motion.inWater && motion.velocity.y <= 0.0 && (move.blockedX || move.blockedZ)) {
        tryStepUp(move, motion, size, world, dt);
    }
    applyMove(motion, move);
    return move;
}

namespace {

// One tick of creative flight (decision E-284): the velocity eases towards the wished one in all
// three directions, without gravity, ladders or stepping up; landing ends the flight. Liquids and
// cobwebs slow it like walking.
void stepFlying(QcEntityMotion &motion, const QcPlayerInput &input, const QcVec3 &wish, const QcBlockAccess &world, double dt)
{
    const QcAabb body = boxAt(motion.position, playerWidth, playerHeight);
    const double inside = slowdownInside(world, body);
    motion.climbing = false;
    motion.slowed = inside < 1.0;

    const double liquid = motion.inWater ? liquidFactorAt(world, blockContaining(motion.position + QcVec3{0.0, 0.4, 0.0})) : 1.0;
    const double factor = liquid * inside;
    const double speed = (input.sprint && input.forward > 0.0f ? flySprintSpeed : flySpeed) * factor;
    const double vertical = double(int(input.jump) - int(input.sneak)) * flyVerticalSpeed * factor;
    const double blend = std::min(1.0, flyAcceleration * dt);
    motion.velocity.x += (wish.x * speed - motion.velocity.x) * blend;
    motion.velocity.z += (wish.z * speed - motion.velocity.z) * blend;
    motion.velocity.y += (vertical - motion.velocity.y) * blend;

    applyMove(motion, moveBox(body, motion.velocity * dt, world));
    if (motion.onGround) {
        motion.flying = false;
    }
}

} // namespace

void stepPlayer(QcEntityMotion &motion, const QcPlayerInput &input, const QcBlockAccess &world, double dt, bool mayFly)
{
    motion.inWater = isLiquidAt(world, blockContaining(motion.position + QcVec3{0.0, 0.4, 0.0}));

    const double yaw = qDegreesToRadians(double(input.yaw));
    const QcVec3 forward{std::sin(yaw), 0.0, -std::cos(yaw)};
    const QcVec3 right{std::cos(yaw), 0.0, std::sin(yaw)};
    QcVec3 wish = forward * std::clamp(double(input.forward), -1.0, 1.0) + right * std::clamp(double(input.strafe), -1.0, 1.0);
    if (wish.length() > 1.0) {
        wish = wish.normalized();
    }

    motion.flying = input.flying && mayFly;
    if (motion.flying) {
        stepFlying(motion, input, wish, world, dt);
        return;
    }

    double speed = walkSpeed;
    if (motion.inWater) {
        speed = swimSpeed;
    } else if (input.sneak) {
        speed = sneakSpeed;
    } else if (input.sprint && input.forward > 0.0f) {
        speed = sprintSpeed;
    }

    stepEntity(motion, {wish, speed, input.jump, input.sneak}, {playerWidth, playerHeight}, world, dt);
}

} // namespace QcPhysics
