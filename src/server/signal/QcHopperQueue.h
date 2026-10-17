#pragma once

#include "common/core/QcBlockPos.h"

#include <QHash>
#include <QList>
#include <QSet>

#include <map>

// Which Signal Hoppers to look at when (decision E-344): those awake for the next hopper step, those
// waiting for a tick (their cooldown ends, an unloaded neighbour may have loaded) and the containers
// whose contents changed. A hopper that finds nothing to do sleeps and costs nothing until something
// around it changes: a block next to it, the contents of a container next to it, an item moving into
// its pickup area. The cooldown itself belongs to the hopper's container (QcBlockEntities::Container).
struct QcHopperQueue
{
    // Ticks after a transfer before the hopper moves the next item (QcServerLimits::hopperCooldownTicks,
    // set by QcServerState; the reference's 8).
    int cooldownTicks = 8;

    // Looks at the hopper in the next step.
    void wake(QcBlockPos pos) { awake.insert(pos); }
    // Looks at the hopper at `tick` (not before).
    void wakeAt(QcBlockPos pos, quint64 tick) { timers[tick].append(pos); }
    // Moves the awake hoppers and those whose timer is due into `woken`.
    void takeDue(quint64 now, QSet<QcBlockPos> &woken);
    void forgetChunk(QcChunkPos chunk);
    void clear();

    QSet<QcBlockPos> awake;
    std::map<quint64, QList<QcBlockPos>> timers;
    // Containers whose contents changed (QcServerState::noteContents): the hoppers next to them wake.
    QSet<QcBlockPos> touched;
};
