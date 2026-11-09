// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/blockentities/QcBlockEntities.h"
#include "server/blockentities/QcSignTexts.h"
#include "server/blocks/QcFireQueue.h"
#include "server/entities/QcEntityComponent.h"
#include "server/machines/QcBeacons.h"
#include "server/machines/QcCampfires.h"
#include "server/signal/QcSignalSchedule.h"
#include "server/spawners/QcSpawners.h"

#include <QHash>
#include <QList>

#include <algorithm>
#include <optional>

struct QcCircuitState;
struct QcHopperQueue;

/*! \brief What the saved block entities of a chunk hold that this version does not read (decision
    E-440), held per chunk by QcServerWorld while the chunk is loaded and written back unchanged:
    tables of unknown ids and the tails of newer versions of known tables (\c {tables},
    QcComponentFrames), and the tails of entries of a newer version (\c {entries}, by position; each
    component's id is its table).

    An entry's tail goes back while the world has an entry of that table at its position. */
struct QcBlockEntityKept
{
    /*! Tables of unknown ids and the tails of newer versions of known tables. */
    QcEntityComponents tables;
    /*! The tails of entries of a newer version, by position. */
    QHash<QcBlockPos, QcEntityComponents> entries;

    /*! Returns whether nothing is kept. */
    bool isEmpty() const { return tables.isEmpty() && entries.isEmpty(); }
    /*! Returns the kept tail of the entry of \a table at \a pos, if any. */
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

/*! \brief What is saved of the block entities of one chunk (decision E-358), with every tick
    counted from the moment of saving, so clocks go on where they stopped when the world opens
    again. */
struct QcBlockEntityRecord
{
    /*! \brief The ids of the tables of a chunk's block entities (decision E-440); a later
        version adds ids below QcComponentFrames::firstModId, a mod from it on; none is ever
        reused. */
    enum class Table : quint16 {
        /*! Containers with contents (\c {containers}). */
        Containers = 1,
        /*! Machines with contents or a cooldown (\c {machines}). */
        Machines = 2,
        /*! Growing saplings (\c {saplings}). */
        Saplings = 3,
        /*! What circuit components remember (\c {memories}). */
        Memories = 4,
        /*! Scheduled ticks of circuit components (\c {scheduled}). */
        Scheduled = 5,
        /*! Positions the circuits were going to look at (\c {wakes}). */
        Wakes = 6,
        /*! Signals that were on their way (\c {signalWakes}). */
        SignalWakes = 7,
        /*! Pending neighbour updates and flow steps (\c {blockUpdates}). */
        BlockUpdates = 8,
        /*! Burning fires (\c {fires}). */
        Fires = 9,
        /*! Monster spawners (decision E-520): kind, delay and confined per spawner. */
        Spawners = 10,
        /*! A marker without entries: the chunk's generated block entities were put in (decision
            E-520). */
        Furnished = 11,
        /*! The items on campfires (decision E-772) with their cooking time. */
        Campfires = 12,
        /*! The text on signs (decision E-766): the lines per sign with text. */
        Signs = 13,
        /*! The effects chosen for beacons (decision E-1142): primary and secondary effect per
            beacon that has one. */
        Beacons = 14,
    };

    /*! \brief What a circuit component remembers; torches' recent switching is not saved. */
    struct Memory
    {
        /*! The component's cell. */
        QcBlockPos pos;
        /*! Ticks until a burnt-out torch may light again; 0: not burnt out. */
        quint32 burntOutLeft = 0;
        /*! Whether it is powered. */
        bool powered = false;
        /*! Ticks since a piston's extension started. */
        std::optional<quint32> extendingAge;
    };

    /*! \brief A scheduled tick of a circuit component: its cell, the ticks left and its
        priority. \qcfields */
    struct Scheduled
    {
        QcBlockPos pos;
        quint32 ticksLeft = 0;
        QcSignalSchedule::Priority priority = QcSignalSchedule::Priority::Diode;
    };

    /*! The chunk. */
    QcChunkPos chunk;
    /*! Containers with contents (or a cooldown); empty ones come back with their block. A
        container's \c {readyAt} holds the ticks left of a hopper's cooldown. */
    QList<QcBlockEntities::Container> containers;
    /*! Machines with contents (or a cooldown); empty ones come back with their block. */
    QList<QcBlockEntities::Machine> machines;
    /*! What was read beyond the room of its block entity (decision E-1066), never written: it
        comes out at the block when the chunk is back. */
    QList<QcBlockEntities::Spill> spills;
    /*! Saplings with the time left until they try to grow. */
    QList<QcBlockEntities::Sapling> saplings;
    /*! What circuit components remember. */
    QList<Memory> memories;
    /*! Scheduled ticks of circuit components. */
    QList<Scheduled> scheduled;
    /*! Positions the circuits were going to look at (changes not yet looked at, positions
        waiting for chunks): looked at again as changes when the chunk is back. */
    QList<QcBlockPos> wakes;
    /*! The same for signals that were on their way (decision E-376): they come back as signals. */
    QList<QcBlockPos> signalWakes;
    /*! Neighbour updates and planned flow steps of the chunk not yet carried out when it was
        unloaded or saved (decision E-374); queued again when it comes back. */
    QList<QcBlockPos> blockUpdates;
    /*! Burning fires with the ticks left to their next step (decision E-392), in the order of
        their steps. */
    QList<QcFireQueue::Saved> fires;
    /*! Monster spawners (decision E-520) with the ticks left to their next cycle. */
    QList<QcSpawners::Entry> spawners;
    /*! The items cooking on campfires (decision E-772). */
    QList<QcCampfires::Entry> campfires;
    /*! Signs with text (decision E-766). */
    QList<QcSignTexts::Entry> signs;
    /*! Beacons with chosen effects (decision E-1142). */
    QList<QcBeacons::Saved> beacons;
    /*! The generator's chest loot and spawner kinds were put into the chunk (QcFurnishing); a
        chunk without the mark gets them when it arrives. */
    bool furnished = false;
    /*! What this version does not read, written back unchanged. */
    QcBlockEntityKept kept;

    /*! Returns whether the record holds nothing (no entries, not furnished, nothing kept). */
    bool isEmpty() const
    {
        return containers.isEmpty() && machines.isEmpty() && saplings.isEmpty() && memories.isEmpty() && scheduled.isEmpty()
               && wakes.isEmpty() && signalWakes.isEmpty() && blockUpdates.isEmpty()
               && fires.isEmpty() && spawners.isEmpty() && campfires.isEmpty() && signs.isEmpty() && beacons.isEmpty() && !furnished && kept.isEmpty();
    }

    /*! Fills the record of a chunk that leaves the world (decision E-374), \c {chunk}, with
        everything of it taken out of \a entities, the work of \a circuits and the queue of
        \a hoppers, ticks counted from \a now; its \c {kept} parts, set by the caller, decide
        like in QcBlockEntityRecords::collect(). \c {blockUpdates} and \c {fires} are left to the
        caller. In QcBlockEntityRecords.cpp. */
    void takeFrom(QcBlockEntities &entities, QcCircuitState &circuits, QcHopperQueue &hoppers, quint64 now);
    /*! Drops the kept tails of entries the record no longer holds (a container broken since it
        was read), once the record is complete (fires included): a tail goes back only with its
        entry. */
    void pruneKept();
};
