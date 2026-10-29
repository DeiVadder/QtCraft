// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"

#include <QHash>
#include <QList>
#include <QSet>

class QcServerWorld;

// The work of the circuits of a world between ticks (decision E-335): block changes near circuits not
// yet looked at, positions waiting for chunks to load and comparators to read again. What components
// remember at their position and their scheduled ticks are block entities (QcBlockEntities, decision
// E-357). Lives in QcServerState; the waiting positions are saved with their chunk.
struct QcCircuitState
{
    // Pending changes beyond this many are dropped; the circuits around them are looked at again with
    // the next change nearby (QcServerLimits::signalMaxPendingChanges, set by QcServerState).
    qsizetype maxPendingChanges = qsizetype{1} << 20;
    // Dropped changes are logged with the first one and then every this many.
    static constexpr quint64 droppedReportInterval = 1 << 12;

    struct Change
    {
        QcBlockPos pos;
        QcBlockId before = qcAirBlockId;
        QcBlockId after = qcAirBlockId;
        // Only the component itself looks again (a chunk loaded next to it); its neighbours do not
        // take it for a block update.
        bool selfOnly = false;
        // A signal on its way (a block changed, a component switched, a reading changed), not a
        // look after a chunk came: where it waits for a chunk that is not loaded, that chunk gets a
        // load ticket (decision E-376).
        bool signal = true;
    };

    // Remembers a block change if a circuit component may lie within two blocks of it (or the block
    // is one); changes far from any circuit cost nothing.
    void noteChange(const QcServerWorld &world, QcBlockPos pos, QcBlockId before, QcBlockId after);
    bool isNearCircuit(const QcServerWorld &world, QcBlockPos pos) const;
    // The contents of the container or machine at `pos` changed: the comparators that read it (behind
    // them, or two behind through a block) look again in the next tick, each once (decision E-343).
    // Without such a comparator it costs the circuits nothing.
    void noteContents(const QcServerWorld &world, QcBlockPos pos);
    // The comparators whose readings may have changed, as changes of themselves; clears them.
    void takeReaders(const QcServerWorld &world);
    // Comparators at `pos + face` or `pos + 2 face` that look away from `pos` (it lies behind them).
    static QList<QcBlockPos> readersOf(const QcServerWorld &world, QcBlockPos pos);
    // Makes the circuits look at `pos` as if its block had just been placed (a demo of the world's
    // setup, a change from a save), now or once its chunk and the eight around it are loaded, so a
    // circuit across a chunk border starts whole.
    void wake(const QcServerWorld &world, QcBlockPos pos);
    // Looks at `pos` again once `chunk` is loaded; a `signal` waiting there asks for a load ticket of
    // `chunk` (decision E-376).
    void waitOn(QcChunkPos chunk, QcBlockPos pos, bool signal = false)
    {
        waitingOn[chunk].append(pos);
        if (signal) {
            signalWaits.insert(pos);
            ticketWishes.insert(chunk);
        }
    }
    // A position from a chunk's record that the circuits were going to look at: a change of itself in
    // the next step, a `signal` on its way or a look (decision E-374).
    void lookAgain(QcBlockPos pos, bool signal) { changes.append({pos, qcAirBlockId, qcAirBlockId, false, signal}); }
    // The chunks signals wait for since the last call.
    QSet<QcChunkPos> takeTicketWishes() { return std::exchange(ticketWishes, {}); }
    // Moves what waited for the chunks around this newly loaded one into the changes when they are
    // all loaded now, and lets the components along its borders with loaded chunks look at each
    // other (a lamp loaded next to powered dust lights). Pistons there are left alone: they wait for
    // a block update as always.
    void chunkLoaded(const QcServerWorld &world, QcChunkPos chunk);
    // Forgets the work of a chunk that was generated again.
    void forgetChunk(QcChunkPos chunk);
    // Takes out every position of a chunk that leaves the world and that the circuits were going to
    // look at (changes, positions waiting for chunks, comparators to read again); they go with the
    // chunk's record and wait again once it is back (decision E-374).
    // Positions the circuits were going to look at: looks (after a chunk came, demos) and signals
    // on their way (E-376: only those ask for load tickets when they come back).
    struct Pending
    {
        QList<QcBlockPos> looks;
        QList<QcBlockPos> onTheirWay;

        void add(QcBlockPos pos, bool signal);
    };
    Pending takeChunk(QcChunkPos chunk);
    // Every position waiting for some chunk or change, by the chunk it lies in (saved with that
    // chunk; read back as positions to look at once their chunk and the eight around it are loaded).
    QHash<QcChunkPos, Pending> pendingByChunk() const;
    void clear();

    QList<Change> changes;
    // Positions to look at once their chunk is loaded.
    QHash<QcChunkPos, QList<QcBlockPos>> waiting;
    // Positions to look at once a chunk next to them is loaded (they read into it).
    QHash<QcChunkPos, QList<QcBlockPos>> waitingOn;
    // Comparators to look at again because what they read changed its contents.
    QSet<QcBlockPos> readers;
    // Positions in waitingOn that wait with a signal, and the chunks signals wait for.
    QSet<QcBlockPos> signalWaits;
    QSet<QcChunkPos> ticketWishes;
    quint64 droppedChanges = 0;

private:
    static bool isSurroundedByLoaded(const QcServerWorld &world, QcChunkPos chunk);
    // The components in the column strip of `chunk` along its side towards `face`.
    void wakeBorder(const QcServerWorld &world, QcChunkPos chunk, QcBlockFace face);
    // The block at `pos` alone, if it is a component other than a piston.
    void wakeOne(const QcServerWorld &world, QcBlockPos pos);
    // Moves the positions waiting for `chunk` into the changes.
    void releaseWaiting(QcChunkPos chunk);
};
