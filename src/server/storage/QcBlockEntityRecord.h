#pragma once

#include "server/blockentities/QcBlockEntities.h"
#include "server/blocks/QcFireQueue.h"
#include "server/entities/QcEntityComponent.h"
#include "server/signal/QcSignalSchedule.h"

#include <QHash>
#include <QList>

#include <algorithm>
#include <optional>

// What the saved block entities of a chunk hold that this version does not read (decision E-440),
// held per chunk by QcServerWorld while the chunk is loaded and written back unchanged: tables of
// unknown ids and the tails of newer versions of known tables (`tables`, QcComponentFrames), and the
// tails of entries of a newer version (`entries`, by position; each component's id is its table).
// An entry's tail goes back while the world has an entry of that table at its position.
struct QcBlockEntityKept
{
    QcEntityComponents tables;
    QHash<QcBlockPos, QcEntityComponents> entries;

    bool isEmpty() const { return tables.isEmpty() && entries.isEmpty(); }
    // The kept tail of the entry of `table` at `pos`, if any.
    const QcEntityComponent *entryTail(quint16 table, QcBlockPos pos) const
    {
        if (entries.isEmpty()) {
            return nullptr;
        }
        const auto it = entries.constFind(pos);
        if (it == entries.constEnd()) {
            return nullptr;
        }
        const auto tail = std::ranges::find(*it, table, &QcEntityComponent::id);
        return tail != it->constEnd() ? &*tail : nullptr;
    }
    bool operator==(const QcBlockEntityKept &) const = default;
};

// What is saved of the block entities of one chunk (decision E-358), with every tick counted from
// the moment of saving, so clocks go on where they stopped when the world opens again.
struct QcBlockEntityRecord
{
    // The ids of the tables of a chunk's block entities (decision E-440); a later version adds ids
    // below QcComponentFrames::firstModId, a mod from it on; none is ever reused.
    enum class Table : quint16 {
        Containers = 1,
        Machines = 2,
        Saplings = 3,
        Memories = 4,
        Scheduled = 5,
        Wakes = 6,
        SignalWakes = 7,
        BlockUpdates = 8,
        Fires = 9,
    };

    // What a circuit component remembers; torches' recent switching is not saved.
    struct Memory
    {
        QcBlockPos pos;
        // Ticks until a burnt-out torch may light again; 0: not burnt out.
        quint32 burntOutLeft = 0;
        bool powered = false;
        // Ticks since a piston's extension started.
        std::optional<quint32> extendingAge;
    };

    struct Scheduled
    {
        QcBlockPos pos;
        quint32 ticksLeft = 0;
        QcSignalSchedule::Priority priority = QcSignalSchedule::Priority::Diode;
    };

    QcChunkPos chunk;
    // Containers and machines with contents (or a cooldown); empty ones come back with their block.
    // A container's `readyAt` holds the ticks left of a hopper's cooldown.
    QList<QcBlockEntities::Container> containers;
    QList<QcBlockEntities::Machine> machines;
    QList<QcBlockEntities::Sapling> saplings;
    QList<Memory> memories;
    QList<Scheduled> scheduled;
    // Positions the circuits were going to look at (changes not yet looked at, positions waiting for
    // chunks): looked at again as changes when the chunk is back.
    QList<QcBlockPos> wakes;
    // The same for signals that were on their way (decision E-376): they come back as signals.
    QList<QcBlockPos> signalWakes;
    // Neighbour updates and planned flow steps of the chunk not yet carried out when it was unloaded
    // or saved (decision E-374); queued again when it comes back.
    QList<QcBlockPos> blockUpdates;
    // Burning fires with the ticks left to their next step (decision E-392), in the order of their steps.
    QList<QcFireQueue::Saved> fires;
    // What this version does not read, written back unchanged.
    QcBlockEntityKept kept;

    bool isEmpty() const
    {
        return containers.isEmpty() && machines.isEmpty() && saplings.isEmpty() && memories.isEmpty() && scheduled.isEmpty()
               && wakes.isEmpty() && signalWakes.isEmpty() && blockUpdates.isEmpty()
               && fires.isEmpty() && kept.isEmpty();
    }
};
