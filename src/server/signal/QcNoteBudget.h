// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"

#include <QHash>

/*! \brief Which notes of note blocks may sound (decision E-767), so that a fast clock driving many
    note blocks cannot flood the clients with sounds: a block plays at most once every
    minIntervalTicks (several notes of one block in one tick become one), and all blocks together at
    most maxPerTick notes a tick.

    A note over either limit is dropped (the block still changes state). */
class QcNoteBudget
{
public:
    /*! Ticks between two notes of one block at least. */
    static constexpr quint64 minIntervalTicks = 4;
    /*! Notes of all blocks in a tick at most. */
    static constexpr int maxPerTick = 32;

    /*! Returns whether the note block at \a pos may play in tick \a tick; counts it if so. */
    [[nodiscard]] bool allow(QcBlockPos pos, quint64 tick);

    /*! Returns the number of blocks remembered (tests): those that played within the last
        minIntervalTicks. */
    [[nodiscard]] qsizetype remembered() const { return m_lastPlayed.size(); }

private:
    // The tick in which each block played last; older entries are dropped when a new tick begins.
    QHash<QcBlockPos, quint64> m_lastPlayed;
    quint64 m_tick = 0;
    int m_playedThisTick = 0;
};
