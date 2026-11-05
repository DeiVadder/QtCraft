// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"

#include <QHash>
#include <QList>
#include <QSet>

#include <map>

/*! \brief Which Signal Hoppers to look at when (decision E-344): those awake for the next hopper
    step, those waiting for a tick (their cooldown ends, an unloaded neighbour may have loaded) and
    the containers whose contents changed.

    A hopper that finds nothing to do sleeps and costs nothing until something around it changes:
    a block next to it, the contents of a container next to it, an item moving into its pickup
    area. The cooldown itself belongs to the hopper's container (QcBlockEntities::Container). */
struct QcHopperQueue
{
    /*! Ticks after a transfer before the hopper moves the next item
        (QcServerLimits::hopperCooldownTicks, set by QcServerState; the reference's 8). */
    int cooldownTicks = 8;

    /*! Looks at the hopper at \a pos in the next step. */
    void wake(QcBlockPos pos) { awake.insert(pos); }
    /*! Looks at the hopper at \a pos at \a tick (not before). */
    void wakeAt(QcBlockPos pos, quint64 tick) { timers[tick].append(pos); }
    /*! Returns the awake hoppers and those whose timer is due at \a now and takes them out. */
    [[nodiscard]] QSet<QcBlockPos> takeDue(quint64 now);
    /*! Forgets the hoppers and containers inside \a chunk. */
    void forgetChunk(QcChunkPos chunk);
    /*! Forgets every hopper and container. */
    void clear();

    /*! Hoppers to look at in the next step. */
    QSet<QcBlockPos> awake;
    /*! Hoppers to look at by the tick of their timer. */
    std::map<quint64, QList<QcBlockPos>> timers;
    /*! Containers whose contents changed (QcServerState::noteContents): the hoppers next to them
        wake. */
    QSet<QcBlockPos> touched;
};
