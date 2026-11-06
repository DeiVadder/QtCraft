// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/mobbrains/QcMobFear.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/physics/QcPhysics.h"
#include "server/core/QcServerWorld.h"

#include <limits>

namespace {

QcVec3 flat(const QcVec3 &vector)
{
    return {vector.x, 0.0, vector.z};
}

// The direction away from `from`, or any direction when it stands on the creature.
QcVec3 awayFrom(const QcVec3 &position, const QcVec3 &from)
{
    const QcVec3 away = flat(position - from);
    return away.length() > 1e-6 ? away.normalized() : QcVec3{1.0, 0.0, 0.0};
}

// The block types of `repellents` that `content` knows.
QList<quint32> repellentTypes(const QcContentRegistry &content, const QcRepellentDefinition &repellents)
{
    QList<quint32> types;
    for (const QString &name : repellents.blocks) {
        if (const std::optional<QcBlockId> block = content.blockId(name)) {
            types.append(content.state(*block).type);
        }
    }
    return types;
}

// Whether the block at `pos` in `world` is of one of `types`.
bool isOfType(const QcBlockAccess &world, QcBlockPos pos, const QList<quint32> &types)
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    return block && *block != qcAirBlockId && types.contains(world.content().state(*block).type);
}

// The nearest block of `repellents` around `feet` in `world` (by distance), none when there is none.
std::optional<QcVec3> nearestRepellent(const QcBlockAccess &world, const QcVec3 &feet, const QcRepellentDefinition &repellents)
{
    const QList<quint32> types = repellentTypes(world.content(), repellents);
    if (types.isEmpty()) {
        return std::nullopt;
    }
    const QcBlockPos centre = QcPhysics::blockContaining(feet);
    std::optional<QcVec3> nearest;
    double best = std::numeric_limits<double>::max();
    const int width = 2 * repellents.range + 1;
    const int cells = width * width * (2 * repellents.height + 1);
    for (int cell = 0; cell < cells; ++cell) {
        const int dx = cell % width - repellents.range;
        const int dz = cell / width % width - repellents.range;
        const int dy = cell / (width * width) - repellents.height;
        const double distance = dx * dx + dy * dy + dz * dz;
        if (distance < best && isOfType(world, centre.offset(dx, dy, dz), types)) {
            best = distance;
            nearest = QcVec3{centre.x + dx + 0.5, static_cast<double>(centre.y + dy), centre.z + dz + 0.5};
        }
    }
    return nearest;
}

} // namespace

namespace QcMobFear {
std::optional<QcVec3> repelledFrom(const QcCreature &mob, const QcMobDefinition &definition, const QcBlockAccess &world)
{
    if (!definition.repellents) {
        return std::nullopt;
    }
    const std::optional<QcVec3> from = nearestRepellent(world, mob.motion.position, *definition.repellents);
    return from ? std::optional<QcVec3>(awayFrom(mob.motion.position, *from)) : std::nullopt;
}


bool calms(const QcMobManager::PlayerView &player, const QcShyDefinition &shy, const QcContentRegistry &content)
{
    return player.held != qcNoItem && content.isValidItem(player.held) && shy.calmItems.contains(content.item(player.held).name)
           && player.speed <= shy.calmSpeed;
}

std::optional<QcVec3> away(const QcCreature &mob, const QcMobDefinition &definition, const QcMobBrain::Context &context)
{
    std::optional<QcVec3> from;
    double nearest = std::numeric_limits<double>::max();
    if (definition.shy && context.players) {
        for (const QcMobManager::PlayerView &player : *context.players) {
            const double distance = (player.position - mob.motion.position).length();
            if (player.alive && distance < definition.shy->distance && distance < nearest
                && !calms(player, *definition.shy, context.world.content())) {
                nearest = distance;
                from = player.position;
            }
        }
    }
    if (definition.scaredOfCats) {
        const QcMobKinds kinds = context.world.content().mobs();
        for (const QcCreature &other : context.mobs) {
            const double distance = (other.motion.position - mob.motion.position).length();
            if (other.id != mob.id && !other.dormant && kinds[other.type].frightens && distance < catDistance && distance < nearest) {
                nearest = distance;
                from = other.motion.position;
            }
        }
    }
    return from ? std::optional<QcVec3>(awayFrom(mob.motion.position, *from)) : std::nullopt;
}

} // namespace QcMobFear
