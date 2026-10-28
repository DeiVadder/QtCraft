#include "server/core/QcServerLimits.h"

#include <QCoreApplication>
#include <QJsonValue>

#include <algorithm>
#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// The groups of the limits for the admin channel's settings (decision E-848).
constexpr const char *signalGroup = QT_TRANSLATE_NOOP("QcServerLimits", "Signal Dust and hoppers");
constexpr const char *creaturesGroup = QT_TRANSLATE_NOOP("QcServerLimits", "Creatures");
constexpr const char *objectsGroup = QT_TRANSLATE_NOOP("QcServerLimits", "Containers, machines and objects");
constexpr const char *blocksGroup = QT_TRANSLATE_NOOP("QcServerLimits", "Block updates and liquids");
constexpr const char *fireGroup = QT_TRANSLATE_NOOP("QcServerLimits", "Fire");
constexpr const char *entitiesGroup = QT_TRANSLATE_NOOP("QcServerLimits", "Items and projectiles");
constexpr const char *explosionsGroup = QT_TRANSLATE_NOOP("QcServerLimits", "Explosions");
constexpr const char *plantsGroup = QT_TRANSLATE_NOOP("QcServerLimits", "Plants");
constexpr const char *chunksGroup = QT_TRANSLATE_NOOP("QcServerLimits", "Chunks");
constexpr const char *villagesGroup = QT_TRANSLATE_NOOP("QcServerLimits", "Villages");

constexpr std::array<QcServerLimits::Field, 72> table{{
    {"signalBudget", QT_TRANSLATE_NOOP("QcServerLimits", "Signal Dust work per tick"), 256, qint64{1} << 22, &QcServerLimits::signalBudget, signalGroup},
    {"signalScheduleReservePercent", QT_TRANSLATE_NOOP("QcServerLimits", "share of the signal budget kept for due scheduled ticks"), 0, 90,
     &QcServerLimits::signalScheduleReservePercent, signalGroup},
    {"signalWorkPerChange", QT_TRANSLATE_NOOP("QcServerLimits", "work one block change of a circuit step counts for"), 1, 64, &QcServerLimits::signalWorkPerChange, signalGroup},
    {"signalMinimumStep", QT_TRANSLATE_NOOP("QcServerLimits", "changes a circuit step takes at least"), 1, 65536, &QcServerLimits::signalMinimumStep, signalGroup},
    {"signalMaxPendingChanges", QT_TRANSLATE_NOOP("QcServerLimits", "block changes near circuits waiting at most"), 1024, qint64{1} << 24,
     &QcServerLimits::signalMaxPendingChanges, signalGroup},
    {"hopperBudget", QT_TRANSLATE_NOOP("QcServerLimits", "Signal Hoppers acting per tick"), 1, qint64{1} << 20, &QcServerLimits::hopperBudget, signalGroup},
    {"hopperCooldownTicks", QT_TRANSLATE_NOOP("QcServerLimits", "ticks a hopper rests after moving an item"), 1, 1200, &QcServerLimits::hopperCooldownTicks, signalGroup},
    {"summonLimit", QT_TRANSLATE_NOOP("QcServerLimits", "awake creatures summoned by /summon (0: none)"), 0, 4096, &QcServerLimits::summonLimit, creaturesGroup},
    {"spawnerCreatures", QT_TRANSLATE_NOOP("QcServerLimits", "awake creatures from monster spawners (0: spawners spawn nothing)"), 0, 4096, &QcServerLimits::spawnerCreatures, creaturesGroup},
    {"guardians", QT_TRANSLATE_NOOP("QcServerLimits", "awake guardians of structure zones such as sand golems (0: none come back)"), 0, 4096, &QcServerLimits::guardians, creaturesGroup},
    {"maxContainers", QT_TRANSLATE_NOOP("QcServerLimits", "containers (chests, barrels, hoppers, dispensers) in the world"), 16, 1 << 20, &QcServerLimits::maxContainers, objectsGroup},
    {"maxMachines", QT_TRANSLATE_NOOP("QcServerLimits", "machines (furnaces) in the world"), 16, 1 << 18, &QcServerLimits::maxMachines, objectsGroup},
    {"maxSaplings", QT_TRANSLATE_NOOP("QcServerLimits", "growing saplings in the world"), 16, 1 << 18, &QcServerLimits::maxSaplings, objectsGroup},
    {"blockUpdatesPerTick", QT_TRANSLATE_NOOP("QcServerLimits", "neighbour updates per tick"), 64, 1 << 20, &QcServerLimits::blockUpdatesPerTick, blocksGroup},
    {"waitingBlockUpdates", QT_TRANSLATE_NOOP("QcServerLimits", "neighbour updates waiting for unloaded chunks"), 1024, 1 << 24, &QcServerLimits::waitingBlockUpdates, blocksGroup},
    {"commandBlockUpdates", QT_TRANSLATE_NOOP("QcServerLimits", "neighbour updates queued by commands"), 1024, 1 << 24, &QcServerLimits::commandBlockUpdates, blocksGroup},
    {"savedBlockUpdates", QT_TRANSLATE_NOOP("QcServerLimits", "pending neighbour updates written to a save"), 1024, qint64{4} << 20,
        &QcServerLimits::savedBlockUpdates, blocksGroup},
    {"liquidStepsPerTick", QT_TRANSLATE_NOOP("QcServerLimits", "flow steps of liquids per tick"), 16, 1 << 20, &QcServerLimits::liquidStepsPerTick, blocksGroup},
    {"liquidPlannedSteps", QT_TRANSLATE_NOOP("QcServerLimits", "flow steps of liquids planned"), 1024, 1 << 24, &QcServerLimits::liquidPlannedSteps, blocksGroup},
    {"fireStepsPerTick", QT_TRANSLATE_NOOP("QcServerLimits", "steps of burning fires per tick (the rest waits)"), 16, 1 << 20, &QcServerLimits::fireStepsPerTick, fireGroup},
    {"firesPerChunk", QT_TRANSLATE_NOOP("QcServerLimits", "burning fires in one chunk"), 1, 65536, &QcServerLimits::firesPerChunk, fireGroup},
    {"fires", QT_TRANSLATE_NOOP("QcServerLimits", "burning fires in the world"), 16, 1 << 20, &QcServerLimits::fires, fireGroup},
    {"fireSpreadRadius", QT_TRANSLATE_NOOP("QcServerLimits", "blocks fire spreads from the spark it began with (0: never)"), 0, 64, &QcServerLimits::fireSpreadRadius, fireGroup},
    {"fallingBlocks", QT_TRANSLATE_NOOP("QcServerLimits", "falling blocks at once"), 16, 65536, &QcServerLimits::fallingBlocks, entitiesGroup},
    {"itemsAwake", QT_TRANSLATE_NOOP("QcServerLimits", "dropped items awake in loaded chunks"), 16, 65536, &QcServerLimits::itemsAwake, entitiesGroup},
    {"itemsQueued", QT_TRANSLATE_NOOP("QcServerLimits", "dropped items waiting to appear"), 16, 1 << 20, &QcServerLimits::itemsQueued, entitiesGroup},
    {"itemsTotal", QT_TRANSLATE_NOOP("QcServerLimits", "dropped items in the world"), 64, 1 << 20, &QcServerLimits::itemsTotal, entitiesGroup},
    {"experienceOrbs", QT_TRANSLATE_NOOP("QcServerLimits", "experience orbs in the world (beyond, experience joins the nearest orb)"), 16, 1 << 20, &QcServerLimits::experienceOrbs, entitiesGroup},
    {"projectiles", QT_TRANSLATE_NOOP("QcServerLimits", "projectiles in the world (beyond, launchers do not shoot)"), 16, 1 << 16, &QcServerLimits::projectiles, entitiesGroup},
    {"projectilesPerChunk", QT_TRANSLATE_NOOP("QcServerLimits", "projectiles stuck in one chunk (beyond, the oldest goes)"), 1, 4096, &QcServerLimits::projectilesPerChunk, entitiesGroup},
    {"primedExplosives", QT_TRANSLATE_NOOP("QcServerLimits", "lit explosives counting down in the world (beyond, an explosive stays a block)"), 0, 65536,
     &QcServerLimits::primedExplosives, explosionsGroup},
    {"explosionsPerTick", QT_TRANSLATE_NOOP("QcServerLimits", "explosions per tick (the rest wait)"), 1, 4096, &QcServerLimits::explosionsPerTick, explosionsGroup},
    {"explosionBlocks", QT_TRANSLATE_NOOP("QcServerLimits", "blocks one explosion breaks at most"), 0, 65536, &QcServerLimits::explosionBlocks, explosionsGroup},
    {"explosionBlocksPerTick", QT_TRANSLATE_NOOP("QcServerLimits", "block changes of explosions per tick (the rest wait)"), 16, 1 << 20, &QcServerLimits::explosionBlocksPerTick, explosionsGroup},
    {"monstersPer289Chunks", QT_TRANSLATE_NOOP("QcServerLimits", "monsters spawning per 289 loaded chunks around players"), 0, 256, &QcServerLimits::monstersPer289Chunks, creaturesGroup},
    {"passiveCreaturesPerPlayer", QT_TRANSLATE_NOOP("QcServerLimits", "passive creatures spawning per player"), 0, 256, &QcServerLimits::passiveCreaturesPerPlayer, creaturesGroup},
    {"ambientCreaturesPerPlayer", QT_TRANSLATE_NOOP("QcServerLimits", "ambient creatures spawning per player"), 0, 256, &QcServerLimits::ambientCreaturesPerPlayer, creaturesGroup},
    {"pathNodesPerTick", QT_TRANSLATE_NOOP("QcServerLimits", "path search nodes of all creatures per tick (0: creatures walk straight at their goals)"), 0, 1 << 20,
     &QcServerLimits::pathNodesPerTick, creaturesGroup},
    {"pathNodesPerSearch", QT_TRANSLATE_NOOP("QcServerLimits", "path search nodes of one search"), 16, 65536, &QcServerLimits::pathNodesPerSearch, creaturesGroup},
    {"pathLength", QT_TRANSLATE_NOOP("QcServerLimits", "waypoints a creature's path keeps"), 8, 256, &QcServerLimits::pathLength, creaturesGroup},
    {"crowdThrottled", QT_TRANSLATE_NOOP("QcServerLimits", "creatures in one block cell from which they think only every 4th to 10th tick"), 2, 4096,
     &QcServerLimits::crowdThrottled, creaturesGroup},
    {"crowdIdle", QT_TRANSLATE_NOOP("QcServerLimits", "creatures in one block cell from which their AI is off (gravity, push and blows at an adjacent player only)"), 2,
     4096, &QcServerLimits::crowdIdle, creaturesGroup},
    {"breedingCrowd", QT_TRANSLATE_NOOP("QcServerLimits", "animals of a kind within 3 x 3 chunks that still breed"), 2, 1024, &QcServerLimits::breedingCrowd, creaturesGroup},
    {"bredPerKind", QT_TRANSLATE_NOOP("QcServerLimits", "bred animals of a kind in the world"), 0, 65536, &QcServerLimits::bredPerKind, creaturesGroup},
    {"randomTicksPerSection", QT_TRANSLATE_NOOP("QcServerLimits", "random ticks per marked section and tick"), 0, 64, &QcServerLimits::randomTicksPerSection, plantsGroup},
    {"leafDecaysPerTick", QT_TRANSLATE_NOOP("QcServerLimits", "leaves decaying per tick"), 1, 4096, &QcServerLimits::leafDecaysPerTick, plantsGroup},
    {"leafChecksPerTick", QT_TRANSLATE_NOOP("QcServerLimits", "leaves checked per tick"), 1, 65536, &QcServerLimits::leafChecksPerTick, plantsGroup},
    {"leafPendingChecks", QT_TRANSLATE_NOOP("QcServerLimits", "leaf checks waiting"), 256, 1 << 20, &QcServerLimits::leafPendingChecks, plantsGroup},
    {"maxArmourStands", QT_TRANSLATE_NOOP("QcServerLimits", "armour stands in the world"), 16, 65536, &QcServerLimits::maxArmourStands, objectsGroup},
    {"maxMinecarts", QT_TRANSLATE_NOOP("QcServerLimits", "minecarts in the world"), 16, 65536, &QcServerLimits::maxMinecarts, objectsGroup},
    {"maxBoats", QT_TRANSLATE_NOOP("QcServerLimits", "boats in the world"), 16, 65536, &QcServerLimits::maxBoats, objectsGroup},
    {"maxHangings", QT_TRANSLATE_NOOP("QcServerLimits", "item frames and paintings in the world"), 16, 65536, &QcServerLimits::maxHangings, objectsGroup},
    {"movingBlocksShown", QT_TRANSLATE_NOOP("QcServerLimits", "blocks shown sliding behind pistons"), 16, 65536, &QcServerLimits::movingBlocksShown, objectsGroup},
    {"chunkGroupLimit", QT_TRANSLATE_NOOP("QcServerLimits", "chunks a group of connected circuits may span before it waits at its edge"), 1, 1024, &QcServerLimits::chunkGroupLimit, chunksGroup},
    {"chunkUnloadsPerTick", QT_TRANSLATE_NOOP("QcServerLimits", "chunks unloaded per tick (groups are always unloaded whole)"), 1, 4096, &QcServerLimits::chunkUnloadsPerTick, chunksGroup},
    {"unsavedChunks", QT_TRANSLATE_NOOP("QcServerLimits", "unloaded chunks kept in memory before a save writes them"), 16, 1048576, &QcServerLimits::unsavedChunks, chunksGroup},
    {"chunkTicketsPerTick", QT_TRANSLATE_NOOP("QcServerLimits", "chunks circuits may load per tick to reach unloaded parts"), 0, 1024, &QcServerLimits::chunkTicketsPerTick, chunksGroup},
    {"chunkTickets", QT_TRANSLATE_NOOP("QcServerLimits", "chunks loaded for circuits at once"), 0, 65536, &QcServerLimits::chunkTickets, chunksGroup},
    {"chunkTicketTicks", QT_TRANSLATE_NOOP("QcServerLimits", "ticks a chunk loaded for circuits stays loaded"), 1, 72000, &QcServerLimits::chunkTicketTicks, chunksGroup},
    {"forcedChunks", QT_TRANSLATE_NOOP("QcServerLimits", "chunks kept loaded by /forceload"), 0, 65536, &QcServerLimits::forcedChunks, chunksGroup},
    {"chunkLoadersPerPlayer", QT_TRANSLATE_NOOP("QcServerLimits", "chunk loaders a player may place"), 0, 1024, &QcServerLimits::chunkLoadersPerPlayer, chunksGroup},
    {"chunkLoaders", QT_TRANSLATE_NOOP("QcServerLimits", "chunk loaders that work in the world"), 0, 65536, &QcServerLimits::chunkLoaders, chunksGroup},
    {"villages", QT_TRANSLATE_NOOP("QcServerLimits", "villages the world keeps"), 0, 65536, &QcServerLimits::villages, villagesGroup},
    {"villageBeds", QT_TRANSLATE_NOOP("QcServerLimits", "beds listed per village"), 0, 1024, &QcServerLimits::villageBeds, villagesGroup},
    {"villageWorkstations", QT_TRANSLATE_NOOP("QcServerLimits", "workstations listed per village"), 0, 1024, &QcServerLimits::villageWorkstations, villagesGroup},
    {"waterCreaturesPerPlayer", QT_TRANSLATE_NOOP("QcServerLimits", "water creatures (fish) spawning per player"), 0, 256, &QcServerLimits::waterCreaturesPerPlayer, creaturesGroup},
    {"villagersPerVillage", QT_TRANSLATE_NOOP("QcServerLimits", "villagers living in one village"), 0, 1024, &QcServerLimits::villagersPerVillage, villagesGroup},
    {"villagers", QT_TRANSLATE_NOOP("QcServerLimits", "villagers living in the world"), 0, 65536, &QcServerLimits::villagers, villagesGroup},
    {"villagerThoughtsPerTick", QT_TRANSLATE_NOOP("QcServerLimits", "villagers deciding what to do per tick"), 1, 65536, &QcServerLimits::villagerThoughtsPerTick, villagesGroup},
    {"villagerRoutesPerTick", QT_TRANSLATE_NOOP("QcServerLimits", "legs of villagers' long walks planned per tick"), 0, 4096, &QcServerLimits::villagerRoutesPerTick, villagesGroup},
    {"villagerHarvestsPerTick", QT_TRANSLATE_NOOP("QcServerLimits", "crops villagers harvest per tick"), 0, 4096, &QcServerLimits::villagerHarvestsPerTick, villagesGroup},
    {"villageGolems", QT_TRANSLATE_NOOP("QcServerLimits", "iron golems in the world beyond which villagers summon none"), 0, 65536, &QcServerLimits::villageGolems, villagesGroup},
}};

} // namespace

std::span<const QcServerLimits::Field> QcServerLimits::fields()
{
    return table;
}

const QcServerLimits::Field *QcServerLimits::field(QStringView key)
{
    const auto found = std::ranges::find_if(table, [key](const Field &candidate) { return key == QLatin1StringView(candidate.key); });
    return found == table.end() ? nullptr : &*found;
}

QcServerLimits::Read QcServerLimits::fromJson(const QJsonObject &json, const QString &source)
{
    Read read;
    QcServerLimits &limits = read.limits;
    const auto warn = [&](const QString &text) { read.warnings.append(u"%1: limits: %2"_s.arg(source, text)); };

    for (auto it = json.constBegin(); it != json.constEnd(); ++it) {
        const auto field = std::ranges::find_if(table, [&it](const Field &candidate) { return it.key() == QLatin1StringView(candidate.key); });
        // Monsters per player became monsters per 289 chunks of all players (decision E-836): an old
        // value (8 in files the server wrote) would mean far too few, so the default applies.
        if (it.key() == u"hostileCreaturesPerPlayer"_s) {
            warn(u"\"hostileCreaturesPerPlayer\" is no longer read; monsters are capped by \"monstersPer289Chunks\" (default %1) now"_s
                     .arg(limits.monstersPer289Chunks));
            continue;
        }
        if (field == table.end()) {
            warn(u"unknown key \"%1\" ignored"_s.arg(it.key()));
            continue;
        }
        const double number = it.value().toDouble(std::nan(""));
        if (!it.value().isDouble() || !std::isfinite(number)) {
            warn(u"\"%1\" is no number, the default %2 stays"_s.arg(it.key()).arg(limits.*(field->value)));
            continue;
        }
        const qint64 wanted = static_cast<qint64>(std::llround(std::clamp(number, -9.0e15, 9.0e15)));
        const qint64 clamped = std::clamp(wanted, field->low, field->high);
        if (clamped != wanted || number != std::floor(number)) {
            warn(u"\"%1\" = %2 is outside %3..%4 (whole numbers), using %5"_s.arg(it.key()).arg(number).arg(field->low).arg(field->high).arg(clamped));
        }
        limits.*(field->value) = clamped;
    }
    return read;
}

QJsonObject QcServerLimits::toJson() const
{
    QJsonObject json;
    for (const Field &field : table) {
        json.insert(QLatin1StringView(field.key), static_cast<double>(this->*(field.value)));
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
