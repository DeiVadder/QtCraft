// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/signal/QcNoteBudget.h"

bool QcNoteBudget::allow(QcBlockPos pos, quint64 tick)
{
    if (tick != m_tick) {
        m_tick = tick;
        m_playedThisTick = 0;
        m_lastPlayed.removeIf([tick](const auto &entry) { return tick - entry.value() >= minIntervalTicks; });
    }
    if (m_playedThisTick >= maxPerTick) {
        return false;
    }

    const auto last = m_lastPlayed.constFind(pos);
    if (last != m_lastPlayed.cend() && tick - *last < minIntervalTicks) {
        return false;
    }
    m_lastPlayed.insert(pos, tick);
    ++m_playedThisTick;
    return true;
}
