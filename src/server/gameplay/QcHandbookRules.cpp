#include "QcHandbookRules.h"

#include "QcBlockCommands.h"
#include "QcChunkRegeneration.h"
#include "QcDayCycle.h"
#include "QcHunger.h"
#include "QcInventory.h"
#include "QcItemEntityManager.h"
#include "QcMining.h"
#include "QcMobManager.h"
#include "QcPendingCommands.h"
#include "QcPhysics.h"
#include "QcRaycast.h"
#include "QcServer.h"
#include "QcServerCombat.h"
#include "QcWorldCommands.h"
#include "QcWorldConstants.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace QcHandbookRules {

QMap<QString, double> values()
{
    return {
        {u"day.length"_s, QcDayCycle::dayLength},
        {u"day.halfLength"_s, QcDayCycle::dayLength / 2.0},
        {u"player.maxHealth"_s, QcServer::playerMaxHealth},
        {u"player.reach"_s, QcRaycast::playerReach},
        {u"player.attackReach"_s, QcServer::attackReach},
        {u"player.attackCooldown"_s, QcServer::playerAttackCooldown},
        {u"player.fallDamageThreshold"_s, QcServer::fallDamageThreshold},
        {u"player.walkSpeed"_s, QcPhysics::walkSpeed},
        {u"player.sprintSpeed"_s, QcPhysics::sprintSpeed},
        {u"player.flySpeed"_s, QcPhysics::flySpeed},
        {u"player.flySprintSpeed"_s, QcPhysics::flySprintSpeed},
        {u"player.sneakSpeed"_s, QcPhysics::sneakSpeed},
        {u"player.swimSpeed"_s, QcPhysics::swimSpeed},
        {u"player.climbSpeed"_s, QcPhysics::climbSpeed},
        {u"player.stepHeight"_s, QcPhysics::stepHeight},
        {u"player.hurtInvulnerability"_s, QcMobManager::hurtInvulnerability},
        {u"inventory.slots"_s, QcInventory::slotCount},
        {u"inventory.hotbar"_s, QcInventory::hotbarSize},
        {u"crafting.tableReach"_s, QcServer::craftingTableReach},
        {u"mining.secondsPerHardness"_s, QcMining::secondsPerHardness},
        {u"mining.secondsPerHardnessWithoutTool"_s, QcMining::secondsPerHardnessWithoutTool},
        {u"hunger.maxFood"_s, QcHunger::maxFood},
        {u"hunger.regenerationFood"_s, QcHunger::regenerationFood},
        {u"hunger.regenerationInterval"_s, QcHunger::regenerationInterval},
        {u"hunger.exhaustionPerFood"_s, QcHunger::exhaustionPerFood},
        {u"hunger.starvationInterval"_s, QcHunger::starvationInterval},
        {u"hunger.starvationFloor"_s, QcHunger::starvationFloor},
        {u"hunger.regenerationFoodPerHealth"_s, QcHunger::regenerationExhaustion / QcHunger::exhaustionPerFood},
        {u"hunger.walkMinutesForFullBar"_s,
         std::round(QcHunger::minutesForFullBar(QcHunger::walkPerMetre, QcPhysics::walkSpeed))},
        {u"hunger.sprintMinutesForFullBar"_s,
         std::round(QcHunger::minutesForFullBar(QcHunger::sprintPerMetre, QcPhysics::sprintSpeed))},
        {u"void.depth"_s, -QcWorld::voidLimitY},
        {u"void.damage"_s, QcServerCombat::voidDamage},
        {u"void.interval"_s, QcServerCombat::voidDamageIntervalTicks * QcPhysics::tickDuration},
        {u"items.despawnSeconds"_s, QcItemEntityManager::despawnSeconds},
        {u"items.thrownPickupDelay"_s, QcItemEntityManager::thrownPickupDelay},
        {u"mobs.hostileCap"_s, QcMobManager::hostileCapPerPlayer},
        {u"mobs.passiveCap"_s, QcMobManager::passiveCapPerPlayer},
        {u"mobs.ambientCap"_s, QcMobManager::ambientCapPerPlayer},
        {u"mobs.spawnRadiusMin"_s, QcMobManager::spawnRadiusMin},
        {u"mobs.spawnRadiusMax"_s, QcMobManager::spawnRadiusMax},
        {u"mobs.despawnDistance"_s, QcMobManager::despawnDistance},
        {u"mobs.caveDepth"_s, QcMobManager::caveDepth},
        {u"commands.maxFill"_s, double(QcServer::maxFillBlocks)},
        {u"commands.summonLimit"_s, QcMobManager::summonLimit},
        {u"commands.confirmSeconds"_s, QcPendingCommands::timeoutSeconds},
        {u"commands.confirmFill"_s, double(QcBlockCommands::confirmFillBlocks)},
        {u"commands.confirmRemove"_s, QcWorldCommands::confirmRemoveCount},
        {u"commands.maxRegenerate"_s, double(QcChunkRegeneration::maxChunks)},
        {u"commands.maxRemoveRadius"_s, QcWorldCommands::maxRemoveRadius},
    };
}

} // namespace QcHandbookRules
