#pragma once

#include "common/core/QcBlockPos.h"

#include <QHash>

// Which notes of note blocks may sound (decision E-767), so that a fast clock driving many note
// blocks cannot flood the clients with sounds: a block plays at most once every minIntervalTicks
// (several notes of one block in one tick become one), and all blocks together at most
// maxPerTick notes a tick. A note over either limit is dropped (the block still changes state).
class QcNoteBudget
{
public:
    static constexpr quint64 minIntervalTicks = 4;
    static constexpr int maxPerTick = 32;

    // Whether the note block at `pos` may play in tick `tick`; counts it if so.
    [[nodiscard]] bool allow(QcBlockPos pos, quint64 tick);

    // Blocks remembered (tests): those that played within the last minIntervalTicks.
    [[nodiscard]] qsizetype remembered() const { return m_lastPlayed.size(); }

private:
    // The tick in which each block played last; older entries are dropped when a new tick begins.
    QHash<QcBlockPos, quint64> m_lastPlayed;
    quint64 m_tick = 0;
    int m_playedThisTick = 0;
};
