#pragma once

#include <QJsonObject>
#include <QStringList>

#include <span>

// Budgets and bounds of the server's simulation that an operator may tune (decision E-349): read
// from the object "limits" of server.json on a dedicated server, of world.json in a single-player
// world (a dedicated server ignores the world's). Every value has a default and a valid range; a
// value out of range is clamped with a warning, an unknown key is reported and ignored. New limits
// are one line in fields().
struct QcServerLimits
{
    struct Field
    {
        const char *key;
        // What it bounds, for /limits, the admin channel and the documentation; marked with
        // QT_TRANSLATE_NOOP("QcServerLimits", ...).
        const char *description;
        qint64 low;
        qint64 high;
        qint64 QcServerLimits::*value;
        // The group the admin channel's settings show it in (decision E-848), marked the same way.
        const char *group;
    };

    // Signal Dust: work per tick (dust looked at, components, pushed blocks; E-335).
    qint64 signalBudget = 8192;
    // Share of the budget (percent) that block changes leave for due scheduled ticks.
    qint64 signalScheduleReservePercent = 25;
    // A step takes at most the budget left divided by this (the work of one change), ...
    qint64 signalWorkPerChange = 4;
    // ... but at least this many changes, so a tick always gets on.
    qint64 signalMinimumStep = 64;
    // Block changes near circuits waiting at most; further ones are dropped with a warning.
    qint64 signalMaxPendingChanges = qint64{1} << 20;
    // Signal Hoppers acting per tick at most (E-344).
    qint64 hopperBudget = 4096;
    // Ticks a hopper rests after moving an item (the reference's 8).
    qint64 hopperCooldownTicks = 8;
    // Creatures summoned by /summon awake at once at most (E-351); natural ones do not count, 0
    // switches summoning creatures off (armour stands are no creatures and stay possible).
    qint64 summonLimit = 64;
    // Awake creatures from monster spawners in the world at most (decision E-520); spawners wait
    // while there are as many. They do not count against the natural caps per player.
    qint64 spawnerCreatures = 256;
    // Awake guardians of structure zones in the world at most (decision E-591; sand golems in
    // desert temples); zones bring forth none while there are as many (0: none at all). Each zone
    // also keeps to its own `max`.
    qint64 guardians = 64;
    // Block entities in the world at most (decision E-357): containers (chests, barrels, hoppers,
    // dispensers), machines (furnaces) and growing saplings. Placing a chest or machine beyond is
    // refused, a sapling beyond does not grow.
    qint64 maxContainers = 16384;
    qint64 maxMachines = 4096;
    qint64 maxSaplings = 4096;
    // Neighbour updates (connections, lost support, starting falls) per tick; those of player actions
    // go first. Updates waiting for unloaded chunks, queued by commands (/fill) and written to a save
    // at most.
    qint64 blockUpdatesPerTick = 1024;
    qint64 waitingBlockUpdates = 65536;
    qint64 commandBlockUpdates = qint64{1} << 20;
    qint64 savedBlockUpdates = qint64{4} << 20;
    // Flow steps of liquids per tick and planned at most (E-054).
    qint64 liquidStepsPerTick = 512;
    qint64 liquidPlannedSteps = 262144;
    // Fire (E-392): steps of fires per tick (each a fire's chance to spread; the rest waits), burning
    // fires per chunk and in the world (beyond, no new fire is lit), and how far fire spreads from the
    // spark it began with (0: fire never spreads, whatever the game rule fireSpread says).
    qint64 fireStepsPerTick = 256;
    qint64 firesPerChunk = 256;
    qint64 fires = 4096;
    qint64 fireSpreadRadius = 12;
    // Falling blocks as entities at once (E-055); beyond, they drop straight or wait (E-288).
    qint64 fallingBlocks = 2048;
    // Dropped items: awake in loaded chunks, waiting to appear, in the world at all.
    qint64 itemsAwake = 512;
    qint64 itemsQueued = 1024;
    qint64 itemsTotal = 4096;
    // Experience orbs in the world (E-398); beyond, dropped experience joins the nearest orb.
    qint64 experienceOrbs = 1024;
    // Projectiles (E-425) in the world (beyond, a launcher does not shoot) and stuck in one chunk
    // (beyond, the oldest stuck there goes).
    qint64 projectiles = 1024;
    qint64 projectilesPerChunk = 64;
    // Explosions (decision E-602): primed explosives in the world at once (beyond, a lit explosive
    // stays a block), explosions per tick (the others wait with their fuse run out), blocks one
    // explosion breaks at most (the nearest first) and block changes of explosions per tick (the
    // rest waits in order).
    qint64 primedExplosives = 512;
    qint64 explosionsPerTick = 8;
    qint64 explosionBlocks = 1024;
    qint64 explosionBlocksPerTick = 2048;
    // Creatures that spawn by themselves, by kind: monsters for every 289 loaded chunks around the
    // players (QcMonsterCap, decision E-836), the others around each player.
    qint64 monstersPer289Chunks = 70;
    qint64 passiveCreaturesPerPlayer = 6;
    qint64 ambientCreaturesPerPlayer = 10;
    // Path finding of creatures (E-400): nodes all searches expand per tick (0 switches path finding
    // off: creatures walk straight at their goals), nodes one search expands at most and waypoints a
    // path keeps at most (a creature at the end of a cut path searches on from there).
    qint64 pathNodesPerTick = 4096;
    qint64 pathNodesPerSearch = 512;
    qint64 pathLength = 64;
    // Crowds of creatures (E-401): from this many awake walkers in one block cell they think only
    // every 4th to 10th tick (no path searches, the last goal stays), from `crowdIdle` on their AI is
    // off (gravity, currents, damage and the push of the crowd only). Blows against an adjacent
    // player stay in both.
    qint64 crowdThrottled = 8;
    qint64 crowdIdle = 24;
    // Breeding (E-293): animals of a kind within 3 x 3 chunks, bred animals of a kind in the world.
    qint64 breedingCrowd = 24;
    qint64 bredPerKind = 256;
    // Random ticks per marked section and tick (growing plants, crops, soil; E-289): more makes
    // plants grow faster, as the reference's randomTickSpeed.
    qint64 randomTicksPerSection = 3;
    // Leaves decaying and checked per tick, checks waiting at most.
    qint64 leafDecaysPerTick = 8;
    qint64 leafChecksPerTick = 64;
    qint64 leafPendingChecks = 16384;
    // Armour stands in the world (E-238), minecarts in the world (E-382; placing more is refused) and
    // blocks shown sliding behind pistons (E-337).
    qint64 maxArmourStands = 4096;
    qint64 maxMinecarts = 4096;
    // Item frames and paintings in the world (E-790; placing more is refused).
    qint64 maxHangings = 4096;
    qint64 movingBlocksShown = 4096;
    // Loading and unloading chunks (stage 31.3, decisions E-374 to E-377): chunks a circuit group may
    // span before it falls back to waiting at the edge; chunks unloaded per tick (whole groups are
    // finished); unloaded chunks kept in memory before a save writes them; load tickets of circuits
    // (new per tick, at once in the world, ticks each lasts); chunks forced by /forceload; chunk
    // loader blocks per player and in the world.
    qint64 chunkGroupLimit = 64;
    qint64 chunkUnloadsPerTick = 32;
    qint64 unsavedChunks = 2048;
    qint64 chunkTicketsPerTick = 4;
    qint64 chunkTickets = 256;
    qint64 chunkTicketTicks = 100;
    qint64 forcedChunks = 256;
    qint64 chunkLoadersPerPlayer = 4;
    qint64 chunkLoaders = 64;
    // Villages (decision E-403): villages the world keeps, beds and workstations listed per village.
    qint64 villages = 4096;
    qint64 villageBeds = 64;
    qint64 villageWorkstations = 64;
    // Villagers (decision E-411): living in one village and in the world at most (no villager spawns
    // beyond), villagers deciding what to do per tick (the others keep their goal a tick longer),
    // legs of long walks planned per tick and crops farmers harvest per tick.
    qint64 villagersPerVillage = 32;
    qint64 villagers = 1024;
    qint64 villagerThoughtsPerTick = 64;
    qint64 villagerRoutesPerTick = 8;
    qint64 villagerHarvestsPerTick = 4;
    // Iron golems in the world beyond which villagers summon no more (decisions E-415, E-470; 0: none).
    qint64 villageGolems = 256;
    // Water creatures (fish, decision E-502) that spawn by themselves per player; they never count
    // against the other caps.
    qint64 waterCreaturesPerPlayer = 5;

    static std::span<const Field> fields();
    // By key; nullptr when there is none.
    static const Field *field(QStringView key);
    // Limits read from a file, and what was clamped or unknown in it.
    struct Read;
    // The defaults with the values of `json` (clamped); the warnings are prefixed with `source`
    // (e.g. "server.json").
    [[nodiscard]] static Read fromJson(const QJsonObject &json, const QString &source);
    QJsonObject toJson() const;
    // "key = value (range low..high): description" per limit.
    QStringList lines() const;

    qint64 scheduleReserve() const { return signalBudget * signalScheduleReservePercent / 100; }
    bool operator==(const QcServerLimits &) const = default;
};

struct QcServerLimits::Read
{
    QcServerLimits limits;
    QStringList warnings;
};
