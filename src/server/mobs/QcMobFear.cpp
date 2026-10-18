#include "server/mobs/QcMobFear.h"

#include "common/content/registry/QcContentRegistry.h"
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

} // namespace

namespace QcMobFear {

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
        const QList<QcMobDefinition> &kinds = context.world.content().mobs();
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
