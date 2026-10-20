#include "worlds/QcHandbookRules.h"

#include "common/content/definitions/QcWeatherDefinition.h"
#include "common/content/registry/QcContentRegistry.h"
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
#include "server/entities/QcHurtWindow.h"
#include "server/explosions/QcBlastBodies.h"
#include "server/explosions/QcExplosionRays.h"
#include "server/gameplay/QcBodyBurning.h"
#include "server/gameplay/QcEnvironmentDamage.h"
#include "server/gameplay/QcHunger.h"
#include "server/gameplay/QcPlayerHealth.h"
#include "server/golems/QcGolemGrudges.h"
#include "server/items/QcExperienceOrbManager.h"
#include "server/items/QcItemEntityManager.h"
#include "server/mobs/QcMobBreeding.h"
#include "server/mobs/QcMobManager.h"
#include "server/mobs/QcNaturalSpawner.h"
#include "server/plants/QcPlantGrowth.h"
#include "server/plants/QcServerPlants.h"
#include "server/signal/QcCircuitComponents.h"
#include "server/villages/QcServerTrading.h"
#include "server/villages/QcVillagePopulation.h"
#include "server/villages/QcVillagerRelocation.h"
#include "server/villages/QcVillages.h"
#include "server/weather/QcServerWeather.h"
#include "server/weather/QcWeatherCycle.h"

#include <cmath>
#include <optional>

using namespace Qt::StringLiterals;

namespace QcHandbookRules {

namespace {

// The power of an explosive whose block has none.
constexpr double defaultPower = 4.0;

// Explosives and explosions (decisions E-600 to E-604): the explosive of the base content (its
// power and fuse; the block's defaults when the content has none) and the limits.
QMap<QString, double> explosionValues(const QcContentRegistry &content)
{
    const std::optional<QcBlockId> explosive = content.blockId(u"qtcraft:explosive"_s);
    const QcBlastDefinition blast = explosive ? content.block(*explosive).blast : QcBlastDefinition{};
    const double power = blast.isExplosive() ? blast.power : defaultPower;
    const QcServerLimits limits;
    return {
        {u"explosions.fuseSeconds"_s, blast.fuseTicks * QcPhysics::tickDuration},
        {u"explosions.reach"_s, QcExplosionRays::reachFactor * power},
        {u"explosions.maxDamage"_s, QcBlastBodies::damageOf(1.0, power)},
        {u"explosions.primed"_s, double(limits.primedExplosives)},
        {u"explosions.perTick"_s, double(limits.explosionsPerTick)},
        {u"explosions.blocks"_s, double(limits.explosionBlocks)},
        {u"explosions.blocksPerTick"_s, double(limits.explosionBlocksPerTick)},
    };
}

// The timing of Signal Dust components (decision E-333 ff.), in ticks, as the signal table of the
// handbook quotes it.
QMap<QString, double> signalValues()
{
    return {
        {u"signal.ticksPerSecond"_s, QcPhysics::tickRate},
        {u"signal.torchDelayTicks"_s, double(QcCircuitComponents::torchDelayTicks)},
        {u"signal.burnoutToggles"_s, double(QcCircuitComponents::burnoutToggles)},
        {u"signal.burnoutWindowTicks"_s, double(QcCircuitComponents::burnoutWindowTicks)},
        {u"signal.repeaterStepTicks"_s, double(QcCircuitComponents::repeaterStepTicks)},
        {u"signal.repeaterMaxTicks"_s, double(4 * QcCircuitComponents::repeaterStepTicks)},
        {u"signal.comparatorDelayTicks"_s, double(QcCircuitComponents::comparatorDelayTicks)},
        {u"signal.observerPulseTicks"_s, double(QcCircuitComponents::observerPulseTicks)},
        {u"signal.dispenserDelayTicks"_s, double(QcCircuitComponents::dispenserDelayTicks)},
    };
}

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
        {u"villagers.wantsFood"_s, double(QcVillagerBreedingSettings{}.shareAbove + QcVillagerBreedingSettings{}.food)},
        {u"villagers.strayDistance"_s, double(QcVillage::reach + QcVillagerHomes::strayReach)},
        {u"villagers.searchRadius"_s, double(QcVillagerRelocation::searchRadius)},
        {u"villagers.cropRadius"_s, double(QcVillagerBehaviour::cropRadius)},
        {u"village.joinDistance"_s, double(QcVillages::joinDistance)},
        {u"golems.minVillagers"_s, double(QcVillageGolemSettings{}.minVillagers)},
        {u"golems.gossipVillagers"_s, double(QcVillageGolemSettings{}.gossipVillagers)},
        {u"golems.gatherDistance"_s, QcVillageGolemSettings{}.gatherDistance},
        {u"golems.golemDistance"_s, QcVillageGolemSettings{}.golemDistance},
        {u"golems.sleptMinutes"_s, QcVillageGolemSettings{}.sleptSeconds / 60.0},
        {u"golems.gossipPercent"_s, QcVillageGolemSettings{}.gossipChance * 100.0},
        {u"golems.checkSeconds"_s, QcVillagePopulation::villageLifeTicks * QcPhysics::tickDuration},
        {u"golems.cooldownSeconds"_s, QcVillageGolemSettings{}.cooldownSeconds},
        {u"golems.spawnRadius"_s, double(QcVillageGolemSettings{}.spawnRadius)},
        {u"golems.grudgeMinutes"_s, QcVillageGolemSettings{}.grudgeSeconds / 60.0},
        {u"golems.angerSeconds"_s, QcGolemGrudges::angerSeconds},
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

// Bows and arrows (decision E-425) and the fishing rod (decision E-503): the defaults the base
// content's bow, arrow and rod keep.
QMap<QString, double> archeryValues()
{
    return {
        {u"projectiles.despawnSeconds"_s, QcProjectileDefinition{}.despawnSeconds},
        {u"projectiles.limit"_s, double(QcServerLimits{}.projectiles)},
        {u"projectiles.perChunk"_s, double(QcServerLimits{}.projectilesPerChunk)},
        {u"bow.drawSeconds"_s, QcLauncherDefinition{}.drawSeconds},
        {u"bow.speed"_s, QcLauncherDefinition{}.speed},
        {u"fishing.minSeconds"_s, QcFishingDefinition{}.minSeconds},
        {u"fishing.maxSeconds"_s, QcFishingDefinition{}.maxSeconds},
        {u"fishing.biteSeconds"_s, QcFishingDefinition{}.biteSeconds},
        {u"fishing.maxDistance"_s, QcFishingDefinition{}.maxDistance},
        {u"boneMeal.stagesMin"_s, double(QcServerPlants::fertilizeStagesMin)},
        {u"boneMeal.stagesMax"_s, double(QcServerPlants::fertilizeStagesMax)},
        {u"boneMeal.saplingChance"_s, 100.0 * QcItemDefinition::fertilizeSaplingChance},
    };
}

// The player's body: health, reach, movement and the invulnerability after a hit with the game
// rules' default and highest length, and the rhythm of contact damage apart from them (decision E-429).
QMap<QString, double> playerValues()
{
    return {
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
        {u"player.hurtInvulnerability"_s, QcHurtWindow::defaultTicks * QcPhysics::tickDuration},
        {u"rules.invulnerabilityTicks"_s, QcHurtWindow::defaultTicks},
        {u"rules.maxInvulnerabilityTicks"_s, QcHurtWindow::maxTicks},
        {u"rules.contactDamageInterval"_s, QcEnvironmentDamage::contactIntervalTicks * QcPhysics::tickDuration},
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

// Food, exhaustion, regeneration and starvation.
QMap<QString, double> hungerValues()
{
    return {
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
    };
}

// Spreading and burning fire, and bodies that burn.
QMap<QString, double> fireValues()
{
    return {
        {u"fire.maxAge"_s, QcFireQueue::maxAge},
        {u"fire.stepSecondsMin"_s, QcFireRules::minStepTicks * QcPhysics::tickDuration},
        {u"fire.stepSecondsMax"_s, (QcFireRules::minStepTicks + QcFireRules::stepTicksSpread - 1) * QcPhysics::tickDuration},
        {u"fire.ageWithoutFuel"_s, QcFireRules::ageWithoutFuel},
        {u"fire.spreadRadius"_s, double(QcServerLimits{}.fireSpreadRadius)},
        {u"fire.perChunk"_s, double(QcServerLimits{}.firesPerChunk)},
        {u"fire.total"_s, double(QcServerLimits{}.fires)},
        {u"fire.burnDamage"_s, QcBodyBurning::burnDamage},
        {u"fire.burnInterval"_s, QcBodyBurning::burnIntervalTicks * QcPhysics::tickDuration},
        {u"fire.sunlightSeconds"_s, QcBodyBurning::sunlightTicks * QcPhysics::tickDuration},
    };
}

// The limits and confirmations of the commands.
QMap<QString, double> commandValues()
{
    return {
        {u"commands.maxFill"_s, double(QcServer::maxFillBlocks)},
        {u"commands.summonLimit"_s, double(QcServerLimits{}.summonLimit)},
        {u"commands.confirmSeconds"_s, QcPendingCommands::timeoutSeconds},
        {u"commands.confirmFill"_s, double(QcBlockCommands::confirmFillBlocks)},
        {u"commands.confirmRemove"_s, QcWorldCommands::confirmRemoveCount},
        {u"commands.maxRegenerate"_s, double(QcChunkRegeneration::maxChunks)},
        {u"commands.maxRemoveRadius"_s, QcWorldCommands::maxRemoveRadius},
    };
}

} // namespace

QMap<QString, double> values(const QcContentRegistry &content)
{
    QMap<QString, double> rules{
        {u"day.length"_s, QcDayCycle::dayLength},
        {u"day.halfLength"_s, QcDayCycle::dayLength / 2.0},
        {u"inventory.slots"_s, QcInventory::slotCount},
        {u"inventory.hotbar"_s, QcInventory::hotbarSize},
        {u"crafting.tableReach"_s, QcServer::craftingTableReach},
        {u"mining.secondsPerHardness"_s, QcMining::secondsPerHardness},
        {u"mining.secondsPerHardnessWithoutTool"_s, QcMining::secondsPerHardnessWithoutTool},
        {u"farming.randomTickSeconds"_s, std::round(QcPlantGrowth::randomTickSeconds)},
        {u"farming.seedCheckSeconds"_s, QcServerPlants::seedCheckTicks * QcPhysics::tickDuration},
        {u"farming.youngFeedShare"_s, 100.0 * QcMobBreeding::feedGrowUpShare},
        {u"signal.pushLimit"_s, QcSignalDefinition{}.pushLimit},
        {u"signal.switchesPerSecond"_s, QcRateLimiter::limit(QcRateLimiter::Kind::Switch).perSecond},
        {u"signal.hopperItemsPerSecond"_s, 1.0 / (double(QcServerLimits{}.hopperCooldownTicks) * QcPhysics::tickDuration)},
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
        {u"mobs.hostileCap"_s, QcNaturalSpawner::hostileCapPerPlayer},
        {u"mobs.passiveCap"_s, QcNaturalSpawner::passiveCapPerPlayer},
        {u"mobs.ambientCap"_s, QcNaturalSpawner::ambientCapPerPlayer},
        {u"mobs.spawnRadiusMin"_s, QcNaturalSpawner::spawnRadiusMin},
        {u"mobs.spawnRadiusMax"_s, QcNaturalSpawner::spawnRadiusMax},
        {u"mobs.despawnDistance"_s, QcMobManager::despawnDistance},
        {u"mobs.caveDepth"_s, QcNaturalSpawner::caveDepth},
    };
    rules.insert(hungerValues());
    rules.insert(fireValues());
    rules.insert(commandValues());
    rules.insert(playerValues());
    rules.insert(weatherValues());
    rules.insert(archeryValues());
    rules.insert(villageValues());
    rules.insert(explosionValues(content));
    rules.insert(signalValues());
    return rules;
}

} // namespace QcHandbookRules
