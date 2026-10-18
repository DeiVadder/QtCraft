#include "server/golems/QcGolemGrudges.h"

#include "common/content/registry/QcContentRegistry.h"
#include "server/core/QcServerWorld.h"
#include "server/entities/QcCreature.h"
#include "server/villages/QcVillage.h"

namespace QcGolemGrudges {

namespace {

// The village whose grudge a blow at `mob` raises: a villager's own, or the one whose area a golem
// stands in; none for other creatures and for golems a player built.
QcVillage *villageOf(QcServerWorld &world, const QcCreature &mob)
{
    if (mob.isVillager()) {
        return mob.villager->village != 0 ? world.villages().find(mob.villager->village) : nullptr;
    }
    if (!mob.golem || mob.golem->playerBuilt) {
        return nullptr;
    }
    const QcVillage *at = world.villages().at(QcPhysics::blockContaining(mob.motion.position));
    return at ? world.villages().find(at->id) : nullptr;
}

} // namespace

void noteBlow(QcServerWorld &world, QcCreature &mob, quint32 player, const QString &name, double worldTime)
{
    if (player == 0) {
        return;
    }
    if (mob.golem && !mob.golem->playerBuilt) {
        mob.golem->angryAt = player;
        mob.golem->angryUntil = worldTime + angerSeconds;
    }

    QcVillage *village = villageOf(world, mob);
    if (!village) {
        return;
    }
    // Grudges that ran out are forgotten, so the list stays as long as the players are many.
    village->grudges.until.removeIf([worldTime](const auto &grudge) { return grudge.value() <= worldTime; });
    if (!name.isEmpty()) {
        village->grudges.until.insert(name, worldTime + world.content().golemOfStyle(village->style).grudgeSeconds);
    }
}

} // namespace QcGolemGrudges
