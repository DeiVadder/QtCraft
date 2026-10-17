#include "server/gameplay/QcHandbookRules.h"

#include "common/content/definitions/QcWeatherDefinition.h"
#include "common/core/QcWorldConstants.h"
#include "common/gameplay/QcExperience.h"
#include "common/gameplay/QcInventory.h"
#include "common/gameplay/QcMining.h"
#include "common/physics/QcPhysics.h"
#include "common/physics/QcRaycast.h"
#include "common/world/QcDayCycle.h"
#include "server/blocks/QcFireQueue.h"
#include "server/blocks/QcFireRules.h"
#include "server/commands/QcBlockCommands.h"
#include "server/commands/QcChunkRegeneration.h"
#include "server/commands/QcPendingCommands.h"
#include "server/commands/QcWorldCommands.h"
#include "server/core/QcRateLimiter.h"
#include "server/core/QcServer.h"
#include "server/core/QcServerLimits.h"
#include "server/gameplay/QcBodyBurning.h"
#include "server/gameplay/QcHunger.h"
#include "server/gameplay/QcPlayerHealth.h"
#include "server/items/QcExperienceOrbManager.h"
#include "server/items/QcItemEntityManager.h"
#include "server/mobs/QcMobBreeding.h"
#include "server/mobs/QcMobManager.h"
#include "server/plants/QcPlantGrowth.h"
#include "server/plants/QcServerPlants.h"
#include "server/villages/QcServerTrading.h"
#include "server/weather/QcServerWeather.h"
#include "server/weather/QcWeatherCycle.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace QcHandbookRules {

namespace {

// The rules of villages, villagers and trading (decisions E-403, E-411, E-414, E-415).
QMap<QString, double> villageValues()
{
    return {
        {u"village.beds"_s, double(QcServerLimits{}.villageBeds)},
        {u"village.workstations"_s, double(QcServerLimits{}.villageWorkstations)},
        {u"villagers.perVillage"_s, double(QcServerLimits{}.villagersPerVillage)},
        {u"villagers.fleeDistance"_s, QcVillagerSettings{}.fleeDistance},
        {u"villagers.breedingFood"_s, double(QcVillagerBreedingSettings{}.food)},
        {u"villagers.shareAbove"_s, double(QcVillagerBreedingSettings{}.shareAbove)},
        {u"villagers.growUpMinutes"_s, QcVillagerBreedingSettings{}.growUpSeconds / 60.0},
        {u"villagers.breedingRestMinutes"_s, QcVillagerBreedingSettings{}.cooldownSeconds / 60.0},
        {u"villagers.max"_s, double(QcServerLimits{}.villagers)},
        {u"golems.minVillagers"_s, double(QcVillageGolemSettings{}.minVillagers)},
        {u"golems.villagersPerGolem"_s, double(QcVillageGolemSettings{}.villagersPerGolem)},
        {u"golems.cooldownMinutes"_s, QcVillageGolemSettings{}.cooldownSeconds / 60.0},
        {u"golems.max"_s, double(QcServerLimits{}.villageGolems)},
        {u"trading.restocksPerDay"_s, double(QcTradingSettings{}.restocksPerDay)},
        {u"trading.offersPerLevel"_s, double(QcTradingSettings{}.offersPerLevel)},
        {u"trading.apprentice"_s, double(QcTradingSettings{}.levels.value(1))},
        {u"trading.journeyman"_s, double(QcTradingSettings{}.levels.value(2))},
        {u"trading.expert"_s, double(QcTradingSettings{}.levels.value(3))},
        {u"trading.master"_s, double(QcTradingSettings{}.levels.value(4))},
        {u"trading.maxRepeats"_s, double(QcServerTrading::maxRepeats)},
    };
}

// The rules of the weather (decisions E-395, E-396).
QMap<QString, double> weatherValues()
{
    return {
        {u"weather.clearMinutesMin"_s, QcWeatherCycle::minClearTicks * QcPhysics::tickDuration / 60.0},
        {u"weather.clearMinutesMax"_s, QcWeatherCycle::maxClearTicks * QcPhysics::tickDuration / 60.0},
        {u"weather.rainMinutesMin"_s, QcWeatherCycle::minRainTicks * QcPhysics::tickDuration / 60.0},
        {u"weather.rainMinutesMax"_s, QcWeatherCycle::maxRainTicks * QcPhysics::tickDuration / 60.0},
        {u"weather.thunderMinutesMin"_s, QcWeatherCycle::minThunderTicks * QcPhysics::tickDuration / 60.0},
        {u"weather.thunderMinutesMax"_s, QcWeatherCycle::maxThunderTicks * QcPhysics::tickDuration / 60.0},
        {u"weather.thunderChance"_s, QcWeatherCycle::thunderChance},
        {u"weather.fadeSeconds"_s, QcWeatherCycle::fadeTicks * QcPhysics::tickDuration},
        {u"weather.snowHeight"_s, QcWeather::snowHeight},
        // The values of the base game; a pack's weather.json may change them.
        {u"weather.strikeSeconds"_s, QcWeatherDefinition{}.lightning.meanSeconds},
        {u"weather.strikeDamage"_s, QcWeatherDefinition{}.lightning.damage},
        {u"weather.strikeRadius"_s, double(QcWeatherDefinition{}.lightning.radius)},
        {u"weather.strikeReach"_s, QcServerWeather::strikeReach},
    };
}

} // namespace

QMap<QString, double> values()
{
    QMap<QString, double> rules{
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
        {u"farming.randomTickSeconds"_s, std::round(QcPlantGrowth::randomTickSeconds)},
        {u"farming.seedCheckSeconds"_s, QcServerPlants::seedCheckTicks * QcPhysics::tickDuration},
        {u"farming.youngFeedShare"_s, 100.0 * QcMobBreeding::feedGrowUpShare},
        {u"signal.pushLimit"_s, QcSignalDefinition{}.pushLimit},
        {u"signal.switchesPerSecond"_s, QcRateLimiter::limit(QcRateLimiter::Kind::Switch).perSecond},
        {u"signal.hopperItemsPerSecond"_s, 1.0 / (double(QcServerLimits{}.hopperCooldownTicks) * QcPhysics::tickDuration)},
        {u"fire.maxAge"_s, QcFireQueue::maxAge},
        {u"fire.stepSecondsMin"_s, QcFireRules::minStepTicks * QcPhysics::tickDuration},
        {u"fire.stepSecondsMax"_s, (QcFireRules::minStepTicks + QcFireRules::stepTicksSpread - 1) * QcPhysics::tickDuration},
        {u"fire.ageWithoutFuel"_s, QcFireRules::ageWithoutFuel},
        {u"fire.spreadRadius"_s, double(QcServerLimits{}.fireSpreadRadius)},
        {u"fire.perChunk"_s, double(QcServerLimits{}.firesPerChunk)},
        {u"fire.total"_s, double(QcServerLimits{}.fires)},
        {u"fire.burnDamage"_s, QcBodyBurning::burnDamage},
        {u"fire.burnInterval"_s, QcBodyBurning::burnIntervalTicks * QcPhysics::tickDuration},
        {u"void.depth"_s, -QcWorld::voidLimitY},
        {u"void.damage"_s, QcPlayerHealth::voidDamage},
        {u"void.interval"_s, QcPlayerHealth::voidDamageIntervalTicks * QcPhysics::tickDuration},
        {u"items.despawnSeconds"_s, QcItemEntityManager::despawnSeconds},
        {u"items.thrownPickupDelay"_s, QcItemEntityManager::thrownPickupDelay},
        {u"experience.despawnSeconds"_s, QcExperienceOrbManager::despawnSeconds},
        {u"experience.attractRange"_s, QcExperienceOrbManager::attractRange},
        {u"experience.deathPerLevel"_s, double(QcExperience::deathPointsPerLevel)},
        {u"experience.deathMax"_s, double(QcExperience::maxDeathPoints)},
        {u"experience.orbLimit"_s, double(QcServerLimits{}.experienceOrbs)},
        {u"mobs.hostileCap"_s, QcMobManager::hostileCapPerPlayer},
        {u"mobs.passiveCap"_s, QcMobManager::passiveCapPerPlayer},
        {u"mobs.ambientCap"_s, QcMobManager::ambientCapPerPlayer},
        {u"mobs.spawnRadiusMin"_s, QcMobManager::spawnRadiusMin},
        {u"mobs.spawnRadiusMax"_s, QcMobManager::spawnRadiusMax},
        {u"mobs.despawnDistance"_s, QcMobManager::despawnDistance},
        {u"mobs.caveDepth"_s, QcMobManager::caveDepth},
        {u"commands.maxFill"_s, double(QcServer::maxFillBlocks)},
        {u"commands.summonLimit"_s, double(QcServerLimits{}.summonLimit)},
        {u"commands.confirmSeconds"_s, QcPendingCommands::timeoutSeconds},
        {u"commands.confirmFill"_s, double(QcBlockCommands::confirmFillBlocks)},
        {u"commands.confirmRemove"_s, QcWorldCommands::confirmRemoveCount},
        {u"commands.maxRegenerate"_s, double(QcChunkRegeneration::maxChunks)},
        {u"commands.maxRemoveRadius"_s, QcWorldCommands::maxRemoveRadius},
    };
    rules.insert(weatherValues());
    rules.insert(villageValues());
    return rules;
}

} // namespace QcHandbookRules
