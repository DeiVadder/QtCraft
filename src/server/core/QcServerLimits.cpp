#include "server/core/QcServerLimits.h"

#include <QJsonValue>

#include <algorithm>
#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr std::array<QcServerLimits::Field, 70> table{{
    {"signalBudget", "Signal Dust work per tick", 256, qint64(1) << 22, &QcServerLimits::signalBudget},
    {"signalScheduleReservePercent", "share of the signal budget kept for due scheduled ticks", 0, 90,
     &QcServerLimits::signalScheduleReservePercent},
    {"signalWorkPerChange", "work one block change of a circuit step counts for", 1, 64, &QcServerLimits::signalWorkPerChange},
    {"signalMinimumStep", "changes a circuit step takes at least", 1, 65536, &QcServerLimits::signalMinimumStep},
    {"signalMaxPendingChanges", "block changes near circuits waiting at most", 1024, qint64(1) << 24,
     &QcServerLimits::signalMaxPendingChanges},
    {"hopperBudget", "Signal Hoppers acting per tick", 1, qint64(1) << 20, &QcServerLimits::hopperBudget},
    {"hopperCooldownTicks", "ticks a hopper rests after moving an item", 1, 1200, &QcServerLimits::hopperCooldownTicks},
    {"summonLimit", "awake creatures summoned by /summon (0: none)", 0, 4096, &QcServerLimits::summonLimit},
    {"spawnerCreatures", "awake creatures from monster spawners (0: spawners spawn nothing)", 0, 4096, &QcServerLimits::spawnerCreatures},
    {"guardians", "awake guardians of structure zones such as sand golems (0: none come back)", 0, 4096, &QcServerLimits::guardians},
    {"maxContainers", "containers (chests, barrels, hoppers, dispensers) in the world", 16, 1 << 20, &QcServerLimits::maxContainers},
    {"maxMachines", "machines (furnaces) in the world", 16, 1 << 18, &QcServerLimits::maxMachines},
    {"maxSaplings", "growing saplings in the world", 16, 1 << 18, &QcServerLimits::maxSaplings},
    {"blockUpdatesPerTick", "neighbour updates per tick", 64, 1 << 20, &QcServerLimits::blockUpdatesPerTick},
    {"waitingBlockUpdates", "neighbour updates waiting for unloaded chunks", 1024, 1 << 24, &QcServerLimits::waitingBlockUpdates},
    {"commandBlockUpdates", "neighbour updates queued by commands", 1024, 1 << 24, &QcServerLimits::commandBlockUpdates},
    {"savedBlockUpdates", "pending neighbour updates written to a save", 1024, qint64(4) << 20, &QcServerLimits::savedBlockUpdates},
    {"liquidStepsPerTick", "flow steps of liquids per tick", 16, 1 << 20, &QcServerLimits::liquidStepsPerTick},
    {"liquidPlannedSteps", "flow steps of liquids planned", 1024, 1 << 24, &QcServerLimits::liquidPlannedSteps},
    {"fireStepsPerTick", "steps of burning fires per tick (the rest waits)", 16, 1 << 20, &QcServerLimits::fireStepsPerTick},
    {"firesPerChunk", "burning fires in one chunk", 1, 65536, &QcServerLimits::firesPerChunk},
    {"fires", "burning fires in the world", 16, 1 << 20, &QcServerLimits::fires},
    {"fireSpreadRadius", "blocks fire spreads from the spark it began with (0: never)", 0, 64, &QcServerLimits::fireSpreadRadius},
    {"fallingBlocks", "falling blocks at once", 16, 65536, &QcServerLimits::fallingBlocks},
    {"itemsAwake", "dropped items awake in loaded chunks", 16, 65536, &QcServerLimits::itemsAwake},
    {"itemsQueued", "dropped items waiting to appear", 16, 1 << 20, &QcServerLimits::itemsQueued},
    {"itemsTotal", "dropped items in the world", 64, 1 << 20, &QcServerLimits::itemsTotal},
    {"experienceOrbs", "experience orbs in the world (beyond, experience joins the nearest orb)", 16, 1 << 20, &QcServerLimits::experienceOrbs},
    {"projectiles", "projectiles in the world (beyond, launchers do not shoot)", 16, 1 << 16, &QcServerLimits::projectiles},
    {"projectilesPerChunk", "projectiles stuck in one chunk (beyond, the oldest goes)", 1, 4096, &QcServerLimits::projectilesPerChunk},
    {"primedExplosives", "lit explosives counting down in the world (beyond, an explosive stays a block)", 0, 65536,
     &QcServerLimits::primedExplosives},
    {"explosionsPerTick", "explosions per tick (the rest wait)", 1, 4096, &QcServerLimits::explosionsPerTick},
    {"explosionBlocks", "blocks one explosion breaks at most", 0, 65536, &QcServerLimits::explosionBlocks},
    {"explosionBlocksPerTick", "block changes of explosions per tick (the rest wait)", 16, 1 << 20, &QcServerLimits::explosionBlocksPerTick},
    {"hostileCreaturesPerPlayer", "hostile creatures spawning per player", 0, 256, &QcServerLimits::hostileCreaturesPerPlayer},
    {"passiveCreaturesPerPlayer", "passive creatures spawning per player", 0, 256, &QcServerLimits::passiveCreaturesPerPlayer},
    {"ambientCreaturesPerPlayer", "ambient creatures spawning per player", 0, 256, &QcServerLimits::ambientCreaturesPerPlayer},
    {"pathNodesPerTick", "path search nodes of all creatures per tick (0: creatures walk straight at their goals)", 0, 1 << 20,
     &QcServerLimits::pathNodesPerTick},
    {"pathNodesPerSearch", "path search nodes of one search", 16, 65536, &QcServerLimits::pathNodesPerSearch},
    {"pathLength", "waypoints a creature's path keeps", 8, 256, &QcServerLimits::pathLength},
    {"crowdThrottled", "creatures in one block cell from which they think only every 4th to 10th tick", 2, 4096,
     &QcServerLimits::crowdThrottled},
    {"crowdIdle", "creatures in one block cell from which their AI is off (gravity, push and blows at an adjacent player only)", 2,
     4096, &QcServerLimits::crowdIdle},
    {"breedingCrowd","animals of a kind within 3 x 3 chunks that still breed", 2, 1024, &QcServerLimits::breedingCrowd},
    {"bredPerKind", "bred animals of a kind in the world", 0, 65536, &QcServerLimits::bredPerKind},
    {"randomTicksPerSection", "random ticks per marked section and tick", 0, 64, &QcServerLimits::randomTicksPerSection},
    {"leafDecaysPerTick", "leaves decaying per tick", 1, 4096, &QcServerLimits::leafDecaysPerTick},
    {"leafChecksPerTick", "leaves checked per tick", 1, 65536, &QcServerLimits::leafChecksPerTick},
    {"leafPendingChecks", "leaf checks waiting", 256, 1 << 20, &QcServerLimits::leafPendingChecks},
    {"maxArmourStands", "armour stands in the world", 16, 65536, &QcServerLimits::maxArmourStands},
    {"maxMinecarts", "minecarts in the world", 16, 65536, &QcServerLimits::maxMinecarts},
    {"movingBlocksShown", "blocks shown sliding behind pistons", 16, 65536, &QcServerLimits::movingBlocksShown},
    {"chunkGroupLimit", "chunks a group of connected circuits may span before it waits at its edge", 1, 1024, &QcServerLimits::chunkGroupLimit},
    {"chunkUnloadsPerTick", "chunks unloaded per tick (groups are always unloaded whole)", 1, 4096, &QcServerLimits::chunkUnloadsPerTick},
    {"unsavedChunks", "unloaded chunks kept in memory before a save writes them", 16, 1048576, &QcServerLimits::unsavedChunks},
    {"chunkTicketsPerTick", "chunks circuits may load per tick to reach unloaded parts", 0, 1024, &QcServerLimits::chunkTicketsPerTick},
    {"chunkTickets", "chunks loaded for circuits at once", 0, 65536, &QcServerLimits::chunkTickets},
    {"chunkTicketTicks", "ticks a chunk loaded for circuits stays loaded", 1, 72000, &QcServerLimits::chunkTicketTicks},
    {"forcedChunks", "chunks kept loaded by /forceload", 0, 65536, &QcServerLimits::forcedChunks},
    {"chunkLoadersPerPlayer", "chunk loaders a player may place", 0, 1024, &QcServerLimits::chunkLoadersPerPlayer},
    {"chunkLoaders", "chunk loaders that work in the world", 0, 65536, &QcServerLimits::chunkLoaders},
    {"villages", "villages the world keeps", 0, 65536, &QcServerLimits::villages},
    {"villageBeds", "beds listed per village", 0, 1024, &QcServerLimits::villageBeds},
    {"villageWorkstations", "workstations listed per village", 0, 1024, &QcServerLimits::villageWorkstations},
    {"waterCreaturesPerPlayer", "water creatures (fish) spawning per player", 0, 256, &QcServerLimits::waterCreaturesPerPlayer},
    {"villagersPerVillage", "villagers living in one village", 0, 1024, &QcServerLimits::villagersPerVillage},
    {"villagers", "villagers living in the world", 0, 65536, &QcServerLimits::villagers},
    {"villagerThoughtsPerTick", "villagers deciding what to do per tick", 1, 65536, &QcServerLimits::villagerThoughtsPerTick},
    {"villagerRoutesPerTick", "legs of villagers' long walks planned per tick", 0, 4096, &QcServerLimits::villagerRoutesPerTick},
    {"villagerHarvestsPerTick", "crops villagers harvest per tick", 0, 4096, &QcServerLimits::villagerHarvestsPerTick},
    {"villageGolems", "iron golems in the world beyond which villagers summon none", 0, 65536, &QcServerLimits::villageGolems},
}};

} // namespace

std::span<const QcServerLimits::Field> QcServerLimits::fields()
{
    return table;
}

QcServerLimits QcServerLimits::fromJson(const QJsonObject &json, const QString &source, QStringList *warnings)
{
    QcServerLimits limits;
    const auto warn = [&](const QString &text) {
        if (warnings) {
            warnings->append(u"%1: limits: %2"_s.arg(source, text));
        }
    };

    for (auto it = json.constBegin(); it != json.constEnd(); ++it) {
        const auto field = std::ranges::find_if(table, [&it](const Field &candidate) { return it.key() == QLatin1StringView(candidate.key); });
        if (field == table.end()) {
            warn(u"unknown key \"%1\" ignored"_s.arg(it.key()));
            continue;
        }
        const double number = it.value().toDouble(std::nan(""));
        if (!it.value().isDouble() || !std::isfinite(number)) {
            warn(u"\"%1\" is no number, the default %2 stays"_s.arg(it.key()).arg(limits.*(field->value)));
            continue;
        }
        const qint64 wanted = qint64(std::llround(std::clamp(number, -9.0e15, 9.0e15)));
        const qint64 clamped = std::clamp(wanted, field->low, field->high);
        if (clamped != wanted || number != std::floor(number)) {
            warn(u"\"%1\" = %2 is outside %3..%4 (whole numbers), using %5"_s.arg(it.key()).arg(number).arg(field->low).arg(field->high).arg(clamped));
        }
        limits.*(field->value) = clamped;
    }
    return limits;
}

QJsonObject QcServerLimits::toJson() const
{
    QJsonObject json;
    for (const Field &field : table) {
        json.insert(QLatin1StringView(field.key), double(this->*(field.value)));
    }
    return json;
}

QStringList QcServerLimits::lines() const
{
    QStringList lines;
    for (const Field &field : table) {
        lines.append(u"%1 = %2 (%3..%4): %5"_s.arg(QLatin1StringView(field.key)).arg(this->*(field.value)).arg(field.low).arg(field.high)
                         .arg(QLatin1StringView(field.description)));
    }
    return lines;
}
