#include "QcMobUpkeep.h"

#include "QcBodyFit.h"
#include "QcWorldConstants.h"

#include <algorithm>
#include <limits>

namespace QcMobUpkeep {

namespace {

// Night creatures stay in daylight while a player is this close.
constexpr double daylightStayDistance = 24.0;

} // namespace

bool despawns(const QcMobManager::Mob &mob, const QcMobDefinition &definition, const QList<QcMobManager::PlayerView> &players,
              bool night)
{
    // The void takes every creature, without loot.
    if (QcWorld::isInVoid(mob.motion.position.y)) {
        return true;
    }
    if (mob.persistent || players.isEmpty()) {
        return false;
    }

    double nearest = std::numeric_limits<double>::max();
    for (const QcMobManager::PlayerView &player : players) {
        nearest = std::min(nearest, (player.position - mob.motion.position).length());
    }

    // Ambient night creatures go out with the daylight even next to a player.
    const bool nightCreature = definition.spawnTime == QcMobDefinition::SpawnTime::Night || mob.nightRule;
    const bool outOfTime = !night && !mob.underground && nightCreature && (nearest > daylightStayDistance || definition.ambient);
    return nearest > QcMobManager::despawnDistance || outOfTime;
}

void leaveBlocks(QcMobManager::Mob &mob, const QcMobDefinition &definition, const QcBlockAccess &world)
{
    // Causes: summoned or bred where a block stands, /fill or /setblock over it, a liquid
    // hardening around it, a growing plant or tree, an old save, a piston without a free cell.
    if (const std::optional<QcVec3> clear = QcBodyFit::clearFeet(world, mob.motion.position, {definition.width, definition.height})) {
        mob.motion.position = *clear;
    }
}

} // namespace QcMobUpkeep
