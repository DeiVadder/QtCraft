#include "app/admin/QcAdminSettingNames.h"

#include <QCoreApplication>

#include <algorithm>
#include <array>

namespace QcAdminSettingNames {

namespace {

struct Name
{
    const char *key;
    const char *text;
};

// The game rules (QcGameRules) and the server limits (QcServerLimits) of this version.
constexpr std::array<Name, 13> ruleNames{{
    {"keepInventory", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Keep inventory")},
    {"playerInvulnerabilityTicks", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Player protection after a hit")},
    {"daylightCycle", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Day and night")},
    {"weatherCycle", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Changing weather")},
    {"spawnChunkRadius", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Loaded area around the spawn")},
    {"mobSpawning", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Creatures spawn")},
    {"mobGriefing", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Creatures change blocks")},
    {"maxEntityCramming", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Crowding damage")},
    {"mobInvulnerabilityTicks", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Creature protection after a hit")},
    {"randomTicks", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Random ticks")},
    {"fireSpread", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Fire spreads")},
    {"quasiConnectivity", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Pistons powered from above")},
    {"explosionBlockDamage", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Explosions damage blocks")},
}};

constexpr std::array<Name, 72> limitNames{{
    {"signalBudget", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Signal work per tick")},
    {"signalScheduleReservePercent", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Reserve for scheduled signal ticks")},
    {"signalWorkPerChange", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Signal work per block change")},
    {"signalMinimumStep", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Smallest circuit step")},
    {"signalMaxPendingChanges", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Waiting changes near circuits")},
    {"hopperBudget", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Hoppers per tick")},
    {"hopperCooldownTicks", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Hopper rest")},
    {"summonLimit", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Summoned creatures")},
    {"spawnerCreatures", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Creatures from spawners")},
    {"guardians", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Guardians of structures")},
    {"maxContainers", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Containers")},
    {"maxMachines", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Machines")},
    {"maxSaplings", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Growing saplings")},
    {"blockUpdatesPerTick", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Block updates per tick")},
    {"waitingBlockUpdates", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Block updates waiting for chunks")},
    {"commandBlockUpdates", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Block updates from commands")},
    {"savedBlockUpdates", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Saved block updates")},
    {"liquidStepsPerTick", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Liquid steps per tick")},
    {"liquidPlannedSteps", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Planned liquid steps")},
    {"fireStepsPerTick", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Fire steps per tick")},
    {"firesPerChunk", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Fires per chunk")},
    {"fires", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Fires")},
    {"fireSpreadRadius", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Fire spread distance")},
    {"fallingBlocks", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Falling blocks")},
    {"itemsAwake", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Active dropped items")},
    {"itemsQueued", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Waiting dropped items")},
    {"itemsTotal", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Dropped items")},
    {"experienceOrbs", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Experience orbs")},
    {"projectiles", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Projectiles")},
    {"projectilesPerChunk", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Projectiles per chunk")},
    {"primedExplosives", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Lit explosives")},
    {"explosionsPerTick", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Explosions per tick")},
    {"explosionBlocks", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Blocks per explosion")},
    {"explosionBlocksPerTick", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Explosion blocks per tick")},
    {"monstersPer289Chunks", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Monsters per 289 chunks")},
    {"passiveCreaturesPerPlayer", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Passive creatures per player")},
    {"ambientCreaturesPerPlayer", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Ambient creatures per player")},
    {"pathNodesPerTick", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Path search per tick")},
    {"pathNodesPerSearch", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Path search per search")},
    {"pathLength", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Path length")},
    {"crowdThrottled", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Crowd that thinks slower")},
    {"crowdIdle", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Crowd without AI")},
    {"breedingCrowd", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Animals that still breed")},
    {"bredPerKind", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Bred animals per kind")},
    {"randomTicksPerSection", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Random ticks per section")},
    {"leafDecaysPerTick", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Leaf decay per tick")},
    {"leafChecksPerTick", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Leaf checks per tick")},
    {"leafPendingChecks", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Waiting leaf checks")},
    {"maxArmourStands", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Armour stands")},
    {"maxMinecarts", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Minecarts")},
    {"maxBoats", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Boats")},
    {"maxHangings", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Item frames and paintings")},
    {"movingBlocksShown", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Sliding blocks shown")},
    {"chunkGroupLimit", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Chunks per circuit group")},
    {"chunkUnloadsPerTick", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Chunk unloads per tick")},
    {"unsavedChunks", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Unsaved chunks in memory")},
    {"chunkTicketsPerTick", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Chunks loaded for circuits per tick")},
    {"chunkTickets", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Chunks loaded for circuits")},
    {"chunkTicketTicks", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Time a circuit chunk stays loaded")},
    {"forcedChunks", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Force-loaded chunks")},
    {"chunkLoadersPerPlayer", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Chunk loaders per player")},
    {"chunkLoaders", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Chunk loaders")},
    {"villages", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Villages")},
    {"villageBeds", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Beds per village")},
    {"villageWorkstations", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Workstations per village")},
    {"waterCreaturesPerPlayer", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Water creatures per player")},
    {"villagersPerVillage", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Villagers per village")},
    {"villagers", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Villagers")},
    {"villagerThoughtsPerTick", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Villager decisions per tick")},
    {"villagerRoutesPerTick", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Villager route legs per tick")},
    {"villagerHarvestsPerTick", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Villager harvests per tick")},
    {"villageGolems", QT_TRANSLATE_NOOP("QcAdminSettingNames", "Iron golems")},
}};

} // namespace

QString name(const QString &key, bool limit)
{
    const auto lookup = [&key](const auto &names) {
        const auto found = std::ranges::find_if(names, [&key](Name entry) { return key == QLatin1StringView(entry.key); });
        return found == names.end() ? QString() : QCoreApplication::translate("QcAdminSettingNames", found->text);
    };
    return limit ? lookup(limitNames) : lookup(ruleNames);
}

} // namespace QcAdminSettingNames
