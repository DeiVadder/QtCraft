// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/chunks/QcChunkGroups.h"
#include "server/chunks/QcChunkTickets.h"
#include "server/chunks/QcSignalChunks.h"
#include "server/dimensions/QcPerDimension.h"
#include "server/storage/QcChunkIo.h"

#include <QHash>
#include <QSet>
#include <QStringList>

#include <optional>
#include <vector>

class QcBlockUpdateQueue;
class QcServerBlocks;
class QcServerState;

/*! \brief Which chunks the server keeps loaded and how they come and go (decisions E-374 to E-377).

    Needed are the chunks around the players (their view and what was sent to them), forced
    chunks (/forceload), the spawn chunks (game rule spawnChunkRadius), the chunks of chunk
    loader blocks and those with a load ticket of the circuits. Chunks come in whole groups
    (QcChunkGroups): an arrived chunk waits until every chunk of its group has arrived, then all
    enter the world in the same tick. A group leaves the world when none of its chunks is needed
    any more: everything attached to its chunks (block entities, scheduled ticks, the circuits'
    and hoppers' work, pending block updates and planned flow steps, burning fires, entities and
    whether the chunk got its animals) is taken out and handed to the storage with the blocks if
    they changed (QcChunkIo), and comes back when the chunk is read again. */
class QcChunkLoading
{
public:
    /*! \brief Numbers for the debug display and /worldinfo. */
    struct Numbers
    {
        /*! Chunks in the world. */
        qsizetype loaded = 0;
        /*! Chunks the storage holds. */
        qsizetype stored = 0;
        /*! Chunks being read or generated, or waiting for the rest of their group. */
        qsizetype waiting = 0;
        /*! Loaded groups of more than one chunk, and the size of the largest. \qcshared */
        qsizetype groups = 0;
        qsizetype largestGroup = 0;
        /*! Groups found larger than the limit. */
        qsizetype oversizeGroups = 0;
        /*! Load tickets now, and tickets refused since the world opened. \qcshared */
        qsizetype tickets = 0;
        quint64 refusedTickets = 0;
        /*! Forced chunks (/forceload). */
        qsizetype forced = 0;
        /*! Spawn chunks. */
        qsizetype spawnChunks = 0;
        /*! Chunk loader blocks in the world. */
        qsizetype loaders = 0;
        /*! Chunks handed to the storage and not written yet. */
        qsizetype unsaved = 0;
        /*! Chunks that left the world since it opened. */
        quint64 unloaded = 0;
    };

    QcChunkLoading(QcServerState &state, QcServerBlocks &blocks, QcBlockUpdateQueue &updates);

    /*! Starts loading the chunk \a pos and the chunks of its group (background; streaming,
        tickets). */
    void request(QcChunkPos pos);
    /*! Loads the chunk \a pos and the chunks of its group now (QcServerWorld::loadChunk). */
    void loadGroupNow(QcChunkPos pos);
    /*! Returns the tick of the next simulation step: the current one from its start until it has
        simulated, the next one after that and between ticks. What a chunk keeps is counted from
        it, so a chunk that leaves after a tick and comes back before one goes on with exactly
        the tick that follows (decision E-374). */
    quint64 upcomingTick() const;
    /*! QcServer: a tick started (after the clock advanced). */
    void beginTick() { m_simulated = false; }
    /*! QcServer: the tick simulated. */
    void noteSimulated() { m_simulated = true; }
    /*! Lets the chunks that arrived in the background into the world, whole groups at a time. */
    void admitArrivals();
    /*! Asks the circuits' waiting chunks for load tickets and lets tickets run out (once per
        tick). */
    void updateTickets();
    /*! Loads the chunks of entities that are in no loaded chunk (\a strays, from
        QcEntities::settle). */
    void requestStrays(const QSet<QcChunkPos> &strays);
    /*! Takes the groups no one needs out of the world (end of a tick). */
    void unloadUnneeded();
    /*! Loads the chunks of stray entities now, so a save sees every entity in a loaded chunk. */
    void loadStraysNow();
    /*! Returns what a save keeps of every loaded chunk (blocks only of changed ones; they count as
        unchanged then). The caller hands it to QcChunkIo::save. */
    QList<QcChunkIo::Departure> departuresForSave();
    /*! Returns the chunks needed now (see the class). */
    QSet<QcChunkPos> neededChunks() const;
    /*! Returns the group of the loaded chunk \a pos as it is now (debug display). */
    QcChunkGroups::Group groupOf(QcChunkPos pos) const;
    /*! Returns the numbers as they are now. */
    Numbers numbers() const;
    /*! Returns the load tickets of the circuits. */
    const QcChunkTickets &tickets() const { return here().tickets; }

private:
    // Lets the waiting chunk at `pos` in with its group if the whole group is there; asks for the rest.
    void admitGroup(QcChunkPos pos);
    // Puts what was kept of a chunk that entered the world back (QcServerWorld's arrival handler).
    void restore(QcChunkIo::Arrival &arrival);
    // The marks of a chunk for a group that is to load: loaded, waiting to be admitted or stored.
    [[nodiscard]] std::optional<quint16> marksForLoading(QcChunkPos pos) const;
    int groupLimit() const;
    // The candidates whose whole group may leave, at most chunkUnloadsPerTick.
    QSet<QcChunkPos> leavingGroups(const QSet<QcChunkPos> &candidates);
    // Takes these chunks out of the world and hands what was kept of them to the storage.
    void unload(const QSet<QcChunkPos> &chunks);
    // Calls `visit(chunk)` for the chunks around each player and those sent to him (some more than once).
    template<typename Visit>
    void forPlayerChunks(Visit visit) const;
    template<typename Visit>
    void forSpawnChunks(Visit visit) const;
    // Forced chunks, spawn chunks and the chunks of chunk loaders (some more than once).
    template<typename Visit>
    void forKeptChunks(Visit visit) const;
    // Gives `chunk` a load ticket for its group and asks for it; false when the limits refuse.
    bool requestTicket(QcChunkPos chunk, quint64 now, bool retry);
    // The chunks loaded for circuits: those with tickets or a waiting signal and the loaded groups
    // around them.
    QSet<QcChunkPos> circuitChunks() const;
    // The marks of a loaded chunk, none for one that is not loaded (groups of the loaded world).
    std::optional<quint16> loadedMarks(QcChunkPos chunk) const;

    // What the needed chunks and the groups depend on: while it stays the same and the last pass let
    // no chunk leave, the next one would let none leave either and is skipped.
    struct NeededInputs
    {
        struct Player
        {
            const void *client = nullptr;
            QcChunkPos centre;
            int distance = 0;
            quint64 sentChanges = 0;

            bool operator==(const Player &) const = default;
        };
        quint64 loadedEpoch = 0;
        quint64 links = 0;
        std::vector<Player> players;
        QList<QcChunkPos> forced;
        int spawnRadius = 0;
        QcChunkPos spawn;
        quint64 loaders = 0;
        qint64 loaderLimit = 0;
        quint64 tickets = 0;
        QSet<QcChunkPos> waitingSignals;

        bool operator==(const NeededInputs &) const = default;
    };
    NeededInputs neededInputs() const;
    void warnOversize(const QcChunkGroups::Group &group);

    QcServerState &m_state;
    QcServerBlocks &m_blocks;
    QcBlockUpdateQueue &m_updates;
    // The current tick has simulated (or none started yet).
    bool m_simulated = true;
    // What the loading of a dimension keeps.
    struct Loading
    {
        Loading(QcServerState &state, const QcBlockUpdateQueue &updates)
            : signalChunks(state, tickets, updates)
        {
        }

        // Arrived chunks waiting for the rest of their group.
        QHash<QcChunkPos, QcChunkIo::Arrival> waiting;
        QHash<QcChunkPos, quint16> waitingMarks;
        // Groups over the limit seen lately (by their first chunk), warned about once.
        QSet<QcChunkPos> oversizeWarned;
        qsizetype oversizeGroups = 0;
        quint64 unloaded = 0;
        // circuitChunks() at the last pass: a chunk the circuits kept until this tick may hold a
        // signal.
        QSet<QcChunkPos> circuitChunks;
        // The inputs of the last pass that let no chunk leave.
        std::optional<NeededInputs> settled;
        QcChunkTickets tickets;
        QcSignalChunks signalChunks;
    };
    // Of the current dimension.
    Loading &here() const;

    QcPerDimension<Loading> m_loading;
};
