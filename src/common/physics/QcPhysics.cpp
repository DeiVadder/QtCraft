#include "common/physics/QcPhysics.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcWorldConstants.h"

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

QcAabb lyingBoxAt(const QcVec3 &feet, float yaw, double width, double height)
{
    // Yaw 0 faces north (-Z), 90 east (+X).
    const double radians = qDegreesToRadians(double(yaw));
    const QcVec3 forward{std::round(std::sin(radians)), 0.0, std::round(-std::cos(radians))};
    const QcVec3 footEnd = feet + forward * lyingPivot;
    const QcVec3 headEnd = feet - forward * (height - lyingPivot);

    const double half = width * 0.5;
    const double acrossX = forward.x == 0.0 ? half : 0.0;
    const double acrossZ = forward.z == 0.0 ? half : 0.0;
    return {{std::min(footEnd.x, headEnd.x) - acrossX, feet.y, std::min(footEnd.z, headEnd.z) - acrossZ},
            {std::max(footEnd.x, headEnd.x) + acrossX, feet.y + half, std::max(footEnd.z, headEnd.z) + acrossZ}};
}

bool isSolidAt(const QcBlockAccess &world, QcBlockPos pos)
{
    if (pos.y < 0) {
        return false;
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

// Whether a collision box of a cell from `start` to z = `lastZ` overlaps `feel`.
bool solidInRow(const QcBlockAccess &world, const QcAabb &feel, QcBlockPos start, int lastZ)
{
    const QcContentRegistry &content = world.content();
    for (int z = start.z; z <= lastZ; ++z) {
        // Most cells around a climber hold nothing to touch (air, plants): no boxes to gather.
        const QcBlockPos pos{start.x, start.y, z};
        const std::optional<QcBlockId> block = pos.y >= 0 ? world.blockAt(pos) : std::nullopt;
        if (block && content.isValidBlock(*block) && content.state(*block).collision.isEmpty()) {
            continue;
        }

        QVarLengthArray<QcAabb, 8> boxes;
        appendCollisionBoxes(world, pos, boxes);
        if (std::ranges::any_of(boxes, [&feel](const QcAabb &solid) { return solid.intersects(feel); })) {
            return true;
        }
    }
    return false;
}

} // namespace

bool clingsToWall(const QcBlockAccess &world, const QcAabb &box)
{
    const double top = box.min.y + std::max(box.max.y - box.min.y, 1.0) + clingAbove;
    // Not the floor it stands on: from a hair above its feet.
    const QcAabb feel{{box.min.x - clingReach, box.min.y + 0.01, box.min.z - clingReach},
                      {box.max.x + clingReach, top, box.max.z + clingReach}};
    const int firstZ = int(std::floor(feel.min.z));
    const int lastZ = int(std::floor(feel.max.z));
    for (int x = int(std::floor(feel.min.x)); x <= int(std::floor(feel.max.x)); ++x) {
        for (int y = int(std::floor(feel.min.y)); y <= int(std::floor(feel.max.y)); ++y) {
            if (solidInRow(world, feel, {x, y, firstZ}, lastZ)) {
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
    return {definition.surfaceSpeed, definition.surfaceJump, definition.slipperiness};
}

double slideKeep(double slipperiness, double drag, double dt)
{
    if (slipperiness <= grippingSlipperiness && drag < coastDrag) {
        return 0.0;
    }
    return std::pow(slipperiness * drag, dt * tickRate);
}

bool isLiquidAt(const QcBlockAccess &world, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    const QcContentRegistry &content = world.content();
    return block && content.isValidBlock(*block) && content.block(*block).liquid;
}

bool isWaterAt(const QcBlockAccess &world, QcBlockPos pos)
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    const QcContentRegistry &content = world.content();
    return block && content.isValidBlock(*block) && content.block(*block).liquid && content.block(*block).fire.setsAlightSeconds == 0;
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

// The vertical velocity in water after one tick: sinking, swimming up with `jump`.
[[nodiscard]] double waterVelocityY(double velocityY, bool jump, double factor, double dt)
{
    velocityY -= waterGravity * dt;
    if (jump) {
        velocityY = std::min(velocityY + swimUpAcceleration * dt, swimUpSpeed * factor);
    }
    return std::max(velocityY, -waterSinkSpeed * factor);
}

// Whether walking in the wished direction runs into a wall right away.
bool pushesIntoWall(const QcEntityMotion &motion, const QcVec3 &wish, BodySize size, const QcBlockAccess &world)
{
    const MoveResult probe = moveBox(boxAt(motion.position, size.width, size.height), {wish.x * 0.05, 0.0, wish.z * 0.05}, world);
    return probe.blockedX || probe.blockedZ;
}

// A low lift for a step under a low ceiling: from a dirt path or farmland (a sixteenth lower) up
// to a full block through a door, whose lintel stops the full step.
constexpr double lowStepHeight = 0.125;

// Walking into something low (a slab, a stair step): the same move lifted by up to stepHeight (or
// lowStepHeight when the full lift gets no further) when it gets further than `move`; none otherwise.
[[nodiscard]] std::optional<MoveResult> steppedUp(const MoveResult &move, const QcEntityMotion &motion, BodySize size, const QcBlockAccess &world,
                                                  double dt)
{
    const QcAabb start = boxAt(motion.position, size.width, size.height);
    const double plain = std::hypot(move.applied.x, move.applied.z);
    for (const double lift : {stepHeight, lowStepHeight}) {
        const MoveResult up = moveBox(start, {0.0, lift, 0.0}, world);
        const QcAabb raised = start.moved(up.applied);
        const MoveResult across = moveBox(raised, {motion.velocity.x * dt, 0.0, motion.velocity.z * dt}, world);
        const QcAabb moved = raised.moved(across.applied);
        const MoveResult down = moveBox(moved, {0.0, -up.applied.y, 0.0}, world);

        const double stepped = std::hypot(across.applied.x, across.applied.z);
        if (stepped > plain && up.applied.y + down.applied.y > 0.0) {
            MoveResult result = move;
            result.applied = up.applied + across.applied + down.applied;
            result.blockedX = across.blockedX;
            result.blockedZ = across.blockedZ;
            result.blockedY = true;
            return result;
        }
    }
    return std::nullopt;
}

} // namespace

} // namespace QcPhysics

using namespace QcPhysics;

double QcEntityMotion::airVelocityY(const Steering &steering, BodySize size, const QcBlockAccess &world, double dt) const
{
    double velocityY = velocity.y;
    if (steering.jump && onGround) {
        // Mud keeps a jump low (decision E-207).
        velocityY = jumpVelocity * surfaceUnder(world, position).jump;
    }
    velocityY = std::max(velocityY - gravity * dt, -terminalVelocity);

    if (climbing) {
        velocityY = std::max(velocityY, steering.hold ? 0.0 : -climbDownSpeed);
        // Jumping or walking into the wall behind the ladder climbs it.
        const bool pushing = steering.wish.length() > 0.0 && pushesIntoWall(*this, steering.wish, size, world);
        if (steering.jump || pushing) {
            velocityY = climbSpeed;
        }
    }
    return velocityY;
}

void QcEntityMotion::applyMove(const MoveResult &move)
{
    position += move.applied;
    onGround = move.blockedY && velocity.y < 0.0;

    // Deep in the void a body stops sinking, without standing on anything (decision E-288).
    if (position.y < QcWorld::voidFloorY) {
        position.y = QcWorld::voidFloorY;
        velocity.y = std::max(0.0, velocity.y);
    }

    if (move.blockedY) {
        velocity.y = 0.0;
    }
    if (move.blockedX) {
        velocity.x = 0.0;
    }
    if (move.blockedZ) {
        velocity.z = 0.0;
    }
}

MoveResult QcEntityMotion::step(const Steering &steering, BodySize size, const QcBlockAccess &world, double dt)
{
    const QcAabb body = boxAt(position, size.width, size.height);
    climbing = !inWater && (touchesClimbable(world, body) || (steering.climbsWalls && clingsToWall(world, body)));
    const double inside = slowdownInside(world, body);
    slowed = inside < 1.0;
    const double acceleration = inWater ? waterAcceleration : (onGround ? groundAcceleration : airAcceleration);
    const SurfaceFactors ground = onGround ? surfaceUnder(world, position) : SurfaceFactors{};
    // On slippery ground (ice) the body keeps most of its velocity: it gains speed and stops slowly.
    const double keep = inWater ? 0.0 : slideKeep(ground.slipperiness, steering.slideDrag, dt);
    const double blend = std::min(1.0, acceleration * dt) * (1.0 - keep);
    // Thick liquids (lava) slow every movement inside them, cobwebs too, and mud slows walking on it.
    const double liquid = inWater ? liquidFactorAt(world, blockContaining(position + QcVec3{0.0, 0.4, 0.0})) : 1.0;
    const double factor = liquid * inside * ground.speed;
    velocity.x += (steering.wish.x * steering.speed * factor - velocity.x) * blend;
    velocity.z += (steering.wish.z * steering.speed * factor - velocity.z) * blend;

    velocity.y = inWater ? waterVelocityY(velocity.y, steering.jump, factor, dt) : airVelocityY(steering, size, world, dt);
    if (slowed) {
        // Sinks slowly through a cobweb and hardly jumps in it.
        velocity.y *= inside;
    }

    MoveResult move = moveBox(boxAt(position, size.width, size.height), velocity * dt, world);
    // A body that just jumped keeps its jump instead of stepping up.
    if (onGround && !inWater && velocity.y <= 0.0 && (move.blockedX || move.blockedZ)) {
        if (const std::optional<MoveResult> stepped = steppedUp(move, *this, size, world, dt)) {
            move = *stepped;
            velocity.y = std::min(velocity.y, 0.0);
        }
    }
    applyMove(move);
    return move;
}

// One tick of creative flight (decision E-284): the velocity eases towards the wished one in all
// three directions, without gravity, ladders or stepping up; landing ends the flight. Liquids and
// cobwebs slow it like walking.
void QcEntityMotion::stepFlying(const QcPlayerInput &input, const QcVec3 &wish, const QcBlockAccess &world, double dt)
{
    const QcAabb body = boxAt(position, playerWidth, playerHeight);
    const double inside = slowdownInside(world, body);
    climbing = false;
    slowed = inside < 1.0;

    const double liquid = inWater ? liquidFactorAt(world, blockContaining(position + QcVec3{0.0, 0.4, 0.0})) : 1.0;
    const double factor = liquid * inside;
    const double speed = (input.sprint && input.forward > 0.0f ? flySprintSpeed : flySpeed) * factor;
    const double vertical = double(int(input.jump) - int(input.sneak)) * flyVerticalSpeed * factor;
    const double blend = std::min(1.0, flyAcceleration * dt);
    velocity.x += (wish.x * speed - velocity.x) * blend;
    velocity.z += (wish.z * speed - velocity.z) * blend;
    velocity.y += (vertical - velocity.y) * blend;

    applyMove(moveBox(body, velocity * dt, world));
    if (onGround) {
        flying = false;
    }
}

void QcEntityMotion::stepPlayer(const QcPlayerInput &input, const QcBlockAccess &world, double dt, bool mayFly)
{
    inWater = isLiquidAt(world, blockContaining(position + QcVec3{0.0, 0.4, 0.0}));

    const double yaw = qDegreesToRadians(double(input.yaw));
    const QcVec3 forward{std::sin(yaw), 0.0, -std::cos(yaw)};
    const QcVec3 right{std::cos(yaw), 0.0, std::sin(yaw)};
    QcVec3 wish = forward * std::clamp(double(input.forward), -1.0, 1.0) + right * std::clamp(double(input.strafe), -1.0, 1.0);
    if (wish.length() > 1.0) {
        wish = wish.normalized();
    }

    flying = input.flying && mayFly;
    if (flying) {
        stepFlying(input, wish, world, dt);
        return;
    }

    double speed = walkSpeed;
    if (inWater) {
        speed = swimSpeed;
    } else if (input.sneak) {
        speed = sneakSpeed;
    } else if (input.sprint && input.forward > 0.0f) {
        speed = sprintSpeed;
    }

    step({wish, speed, input.jump, input.sneak}, {playerWidth, playerHeight}, world, dt);
}
