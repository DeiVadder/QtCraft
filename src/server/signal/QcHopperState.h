#pragma once

#include "QcBlockPos.h"

#include <QHash>
#include <QList>
#include <QSet>

#include <map>

// What the Signal Hoppers of a world remember between ticks (decision E-344): the hoppers awake for
// the next hopper step, those waiting for a tick (their cooldown ends, an unloaded neighbour may have
// loaded) and the cooldowns. A hopper that finds nothing to do sleeps and costs nothing until
// something around it changes: a block next to it, the contents of a container next to it, an item
// moving into its pickup area.
struct QcHopperState
{
    // Ticks after a transfer before the hopper moves the next item (QcServerLimits::hopperCooldownTicks,
    // set by QcServerState; the reference's 8).
    int cooldownTicks = 8;

    // Looks at the hopper in the next step.
    void wake(QcBlockPos pos) { awake.insert(pos); }
    // Looks at the hopper at `tick` (not before).
    void wakeAt(QcBlockPos pos, quint64 tick) { timers[tick].append(pos); }
    // The hopper moved an item at `now`: it rests until the cooldown ends and looks again then.
    void startCooldown(QcBlockPos pos, quint64 now);
    bool isCoolingDown(QcBlockPos pos, quint64 now) const { return readyAt.value(pos, 0) > now; }
    // Moves the awake hoppers and those whose timer is due into `woken`.
    void takeDue(quint64 now, QSet<QcBlockPos> &woken);
    // A piston moved the hopper with its cooldown.
    void relocate(QcBlockPos from, QcBlockPos to);
    void prune(quint64 now);
    void forgetChunk(QcChunkPos chunk);
    void clear();

    QSet<QcBlockPos> awake;
    std::map<quint64, QList<QcBlockPos>> timers;
    // Saved in signals.qce, so a cooldown goes on after loading.
    QHash<QcBlockPos, quint64> readyAt;
    // Containers whose contents changed (QcServerState::noteContents): the hoppers next to them wake.
    QSet<QcBlockPos> touched;
};
