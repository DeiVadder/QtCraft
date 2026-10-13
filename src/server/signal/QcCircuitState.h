#pragma once

#include "QcBlockDefinition.h"
#include "QcHopperState.h"
#include "QcSignalSchedule.h"

#include <QHash>
#include <QList>
#include <QSet>

class QcServerWorld;

// What the circuits of a world remember between ticks (decision E-335): block changes near circuits
// not yet looked at, scheduled ticks, recent switching of torches, openers last seen powered and
// pistons in the middle of extending. Lives in QcServerState; saved in signals.qce.
struct QcCircuitState
{
    // Pending changes beyond this many are dropped; the circuits around them are looked at again with
    // the next change nearby (QcServerLimits::signalMaxPendingChanges, set by QcServerState).
    qsizetype maxPendingChanges = qsizetype(1) << 20;
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
    };

    // Remembers a block change if a circuit component may lie within two blocks of it (or the block
    // is one); changes far from any circuit cost nothing.
    void noteChange(const QcServerWorld &world, QcBlockPos pos, QcBlockId before, QcBlockId after);
    bool isNearCircuit(const QcServerWorld &world, QcBlockPos pos) const;
    // The contents of the container or machine at `pos` changed: hoppers next to it wake, and the
    // comparators that read it (behind them, or two behind through a block) look again in the next
    // tick, each once (decision E-343). Without such a comparator it costs the circuits nothing.
    void noteContents(const QcServerWorld &world, QcBlockPos pos);
    // The comparators whose readings may have changed, as changes of themselves; clears them.
    void takeReaders(const QcServerWorld &world);
    // Comparators at `pos + face` or `pos + 2 face` that look away from `pos` (it lies behind them).
    static QList<QcBlockPos> readersOf(const QcServerWorld &world, QcBlockPos pos);
    // Makes the circuits look at `pos` as if its block had just been placed (a demo of the world's
    // setup, a change from a save), now or once its chunk and the eight around it are loaded, so a
    // circuit across a chunk border starts whole.
    void wake(const QcServerWorld &world, QcBlockPos pos);
    // Looks at `pos` again once `chunk` is loaded.
    void waitOn(QcChunkPos chunk, QcBlockPos pos) { waitingOn[chunk].append(pos); }
    // Moves what waited for the chunks around this newly loaded one into the changes when they are
    // all loaded now, and lets the components along its borders with loaded chunks look at each
    // other (a lamp loaded next to powered dust lights). Pistons there are left alone: they wait for
    // a block update as always.
    void chunkLoaded(const QcServerWorld &world, QcChunkPos chunk);
    // Drops what is remembered for `pos` that no longer fits the kind of block there now (a door
    // replaced by a lever, a torch broken).
    void forgetMemory(QcBlockPos pos, QcSignalKind now);
    // Drops memories that ran out: torches that have not gone out within `window` ticks, ended
    // burnouts and extensions.
    void prune(quint64 now, quint64 window);
    // Forgets everything of a chunk that was generated again.
    void forgetChunk(QcChunkPos chunk);
    void clear();

    QList<Change> changes;
    // Positions to look at once their chunk is loaded.
    QHash<QcChunkPos, QList<QcBlockPos>> waiting;
    // Positions to look at once a chunk next to them is loaded (they read into it).
    QHash<QcChunkPos, QList<QcBlockPos>> waitingOn;
    QcSignalSchedule schedule;
    // Ticks at which each torch went out lately (for burning out) and torches burnt out until a tick.
    QHash<QcBlockPos, QList<quint64>> torchOffTicks;
    QHash<QcBlockPos, quint64> burntOutUntil;
    // Openers (the lower half of doors) that were powered when last looked at: they only switch when
    // that changes, so players can still open and close them by hand.
    QSet<QcBlockPos> poweredOpeners;
    // Pistons whose extension started at a tick and lasts QcPistonMover::moveTicks: retracting before
    // it ends lets a sticky piston drop its block (block spitting).
    QHash<QcBlockPos, quint64> extending;
    QcHopperState hoppers;
    // Comparators to look at again because what they read changed its contents.
    QSet<QcBlockPos> readers;
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
