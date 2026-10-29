// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/mobbrains/QcMobHover.h"

#include "common/content/mobs/QcMobDefinition.h"
#include "server/entities/QcCreature.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace QcMobHover {

namespace {

// A number in [0, 1) fixed by an id (orbit radius and phase of a child).
double share(quint32 id, quint32 salt)
{
    const quint32 hash = (id ^ salt) * 2654435761u;
    return static_cast<double>(hash >> 8) / static_cast<double>(1u << 24);
}

// The top of what carries a hoverer in the cell `pos` (a collision box or a liquid), if anything.
std::optional<double> floorIn(const QcBlockAccess &world, QcBlockPos pos)
{
    const QList<QcAabb> boxes = QcPhysics::collisionBoxesAt(world, pos);
    if (!boxes.isEmpty()) {
        return std::ranges::max(boxes, {}, [](const QcAabb &box) { return box.max.y; }).max.y;
    }
    if (QcPhysics::isLiquidAt(world, pos)) {
        return pos.y + 1.0;
    }
    return std::nullopt;
}

// The bottom of what stops a head in the cell `pos`, if anything.
std::optional<double> ceilingIn(const QcBlockAccess &world, QcBlockPos pos)
{
    const QList<QcAabb> boxes = QcPhysics::collisionBoxesAt(world, pos);
    if (boxes.isEmpty()) {
        return std::nullopt;
    }
    return std::ranges::min(boxes, {}, [](const QcAabb &box) { return box.min.y; }).min.y;
}

} // namespace

double vertical(double velocity, double rise, bool knockedBack)
{
    // While knocked back it may sink towards its height, but never faster than it would rise or
    // sink there by itself, and never below it: a hit without a push (fire, cramming) keeps it up.
    const double least = knockedBack ? std::min(rise, 0.0) : rise;
    const double fade = knockedBack ? QcPhysics::gravity : liftFade;
    return std::max(velocity - fade * QcPhysics::tickDuration, least);
}

Room roomAt(const QcBlockAccess &world, const QcVec3 &feet, double height, double reach)
{
    Room room{feet.y - reach, std::nullopt};
    const QcBlockPos start = QcPhysics::blockContaining(feet + QcVec3{0.0, height * 0.5, 0.0});
    for (int down = 0; down <= static_cast<int>(std::ceil(reach + height)) + 1; ++down) {
        const QcBlockPos cell = start.offset(0, -down, 0);
        // Only what lies below the middle of the body carries it.
        if (const std::optional<double> top = floorIn(world, cell); top && *top <= feet.y + height * 0.5) {
            room.floor = *top;
            break;
        }
    }
    for (int up = 0; up <= static_cast<int>(std::ceil(height)) + 2; ++up) {
        const QcBlockPos cell = start.offset(0, up, 0);
        if (const std::optional<double> bottom = ceilingIn(world, cell); bottom && *bottom >= feet.y + height * 0.5) {
            room.ceiling = bottom;
            break;
        }
    }
    return room;
}

double wantedFeet(const QcCreature &mob, const QcMobDefinition &definition, const Room &room, double seconds)
{
    if (!definition.hover) {
        return mob.motion.position.y;
    }
    const QcHoverDefinition &hover = *definition.hover;
    const double phase = 2.0 * std::numbers::pi * (seconds / hover.bobSeconds + share(mob.id, 0x5bd1e995u));
    double feet = room.floor + hover.height + hover.bob * std::sin(phase);
    // Over a wall in its way: up from where it is.
    if (mob.climbTicks > 0) {
        feet = std::max(feet, mob.motion.position.y + climbHeight);
    }
    feet = std::min(feet, room.floor + hover.maxHeight);
    if (room.ceiling) {
        feet = std::min(feet, *room.ceiling - definition.height - ceilingGap);
    }
    return std::max(feet, room.floor);
}

void fly(QcCreature &mob, const QcMobDefinition &definition, const QcBlockAccess &world, const Course &course, double seconds)
{
    if (!definition.hover) {
        return;
    }
    const QcHoverDefinition &hover = *definition.hover;
    const double dt = QcPhysics::tickDuration;
    const Room room = roomAt(world, mob.motion.position, definition.height, hover.maxHeight + 1.0);
    // An orbit point may lie below the floor or above the highest hover: its feet stay between.
    double feet = course.feetY.value_or(wantedFeet(mob, definition, room, seconds));
    feet = std::min(feet, room.floor + hover.maxHeight);
    if (room.ceiling) {
        feet = std::min(feet, *room.ceiling - definition.height - ceilingGap);
    }
    feet = std::max(feet, room.floor);
    mob.climbTicks = std::max(0, mob.climbTicks - 1);

    // Sideways towards the wish, unless knocked back (then it drifts and slows) or without a mind.
    const double rise = std::clamp((feet - mob.motion.position.y) * verticalGain, -maxVerticalSpeed, maxVerticalSpeed);
    const bool steers = !mob.hurt.knockedBack() && !mob.noAi;
    const QcVec3 wish = steers ? course.steering.wish * course.steering.speed : QcVec3{};
    const double blend = std::min(1.0, (steers ? steerRate : steerRate * 0.25) * dt);
    QcVec3 &velocity = mob.motion.velocity;
    velocity.x += (wish.x - velocity.x) * blend;
    velocity.z += (wish.z - velocity.z) * blend;
    velocity.y = vertical(velocity.y, rise, mob.hurt.knockedBack());

    const QcPhysics::MoveResult move =
        QcPhysics::moveBox(QcPhysics::boxAt(mob.motion.position, definition.width, definition.height), velocity * dt, world);
    mob.motion.position += move.applied;
    mob.motion.onGround = false;
    mob.motion.inWater = false;
    mob.blockedLastTick = (move.blockedX || move.blockedZ) && steers && course.steering.wish.length() > 0.0;
    if (mob.blockedLastTick) {
        mob.climbTicks = climbTicks;
    }
    if (move.blockedX) {
        velocity.x = 0.0;
    }
    if (move.blockedZ) {
        velocity.z = 0.0;
    }
    if (move.blockedY) {
        velocity.y = 0.0;
    }
}

QcVec3 orbitPoint(quint32 child, const QcVec3 &parentFeet, double parentHeight, const QcFamilyDefinition &family, double seconds)
{
    const double radius = family.orbitRadius * (0.7 + 0.6 * share(child, 0x9e3779b9u));
    const double direction = (child & 1u) != 0 ? 1.0 : -1.0;
    const double phase = 2.0 * std::numbers::pi * share(child, 0x85ebca6bu);
    const double angle = direction * 2.0 * std::numbers::pi * seconds / family.orbitSeconds + phase;
    // A little up and down along the orbit, so the children do not circle in one plane.
    const double tilt = 0.25 * parentHeight * std::sin(angle * 2.0 + phase);
    return parentFeet + QcVec3{radius * std::cos(angle), parentHeight * 0.3 + tilt, radius * std::sin(angle)};
}

Course towards(const QcCreature &mob, const QcVec3 &point, double speed)
{
    const QcVec3 flat{point.x - mob.motion.position.x, 0.0, point.z - mob.motion.position.z};
    const double distance = flat.length();
    if (distance < 1e-6) {
        return {{{}, 0.0}, point.y};
    }
    return {{flat * (1.0 / distance), std::min(speed, distance * verticalGain)}, point.y};
}

float turned(float from, float to, double degrees)
{
    const double delta = std::remainder(static_cast<double>(to) - static_cast<double>(from), 360.0);
    return static_cast<float>(static_cast<double>(from) + std::clamp(delta, -degrees, degrees));
}

} // namespace QcMobHover
