// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/mobs/QcMobUpkeep.h"

#include "common/core/QcWorldConstants.h"
#include "common/physics/QcBodyFit.h"

#include <algorithm>
#include <limits>
#include <optional>

namespace QcMobUpkeep {

namespace {

// Night creatures stay in daylight while a player is this close.
constexpr double daylightStayDistance = 24.0;

// Distance to the nearest player with a body; none without one.
std::optional<double> nearestPlayer(const QcCreature &mob, const QList<QcMobManager::PlayerView> &players)
{
    std::optional<double> nearest;
    for (const QcMobManager::PlayerView &player : players) {
        if (player.alive) {
            nearest = std::min(nearest.value_or(std::numeric_limits<double>::max()), (player.position - mob.motion.position).length());
        }
    }
    return nearest;
}

// The fate of a creature that is not in the void, `nearest` metres from the nearest player and idle
// for `idle` ticks.
Fate fateAt(const QcCreature &mob, const QcMobDefinition &definition, std::optional<double> nearest, bool night, int idle)
{
    if (mob.persistent || !nearest) {
        return Fate::Stays;
    }

    // Ambient night creatures go out with the daylight even next to a player.
    const bool nightCreature = definition.spawnTime == QcMobDefinition::SpawnTime::Night || mob.nightRule;
    const bool outOfTime = !night && !mob.underground && nightCreature && (*nearest > daylightStayDistance || definition.ambient);
    if (*nearest > QcMobManager::despawnDistance || outOfTime) {
        return Fate::Despawns;
    }
    // Guardians keep their zone (decision E-591), which brings new ones only under its conditions.
    return idle > idleTicks && *nearest > idleDistance && !mob.guards ? Fate::MayDespawn : Fate::Stays;
}

} // namespace

Upkeep upkeepOf(const QcCreature &mob, const QcMobDefinition &definition, const QList<QcMobManager::PlayerView> &players, bool night)
{
    // One distance per creature and tick serves the timer and the fate.
    const std::optional<double> nearest = nearestPlayer(mob, players);
    const int idle = !nearest || *nearest <= idleDistance ? 0 : std::min(mob.idleTicks, idleTicks) + 1;
    // The void takes every creature, without loot.
    if (QcWorld::isInVoid(mob.motion.position.y)) {
        return {Fate::Despawns, idle};
    }
    return {fateAt(mob, definition, nearest, night, idle), idle};
}

void leaveBlocks(QcCreature &mob, const QcMobDefinition &definition, const QcBlockAccess &world)
{
    // Causes: summoned or bred where a block stands, /fill or /setblock over it, a liquid
    // hardening around it, a growing plant or tree, an old save, a piston without a free cell.
    if (const std::optional<QcVec3> clear = QcBodyFit::clearFeet(world, mob.motion.position, {definition.width, definition.height})) {
        mob.motion.position = *clear;
    }
}

} // namespace QcMobUpkeep
