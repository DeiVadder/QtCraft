// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/tick/QcTickPacer.h"

#include <algorithm>

QcTickPacer::QcTickPacer(qint64 intervalNs)
    : m_intervalNs(intervalNs)
{
}

void QcTickPacer::start(qint64 nowNs)
{
    m_nextDueNs = nowNs + m_intervalNs;
}

int QcTickPacer::take(qint64 nowNs)
{
    if (nowNs < m_nextDueNs) {
        return 0;
    }

    const qint64 due = (nowNs - m_nextDueNs) / m_intervalNs + 1;
    if (due > maxCatchUp) {
        // The oldest ones are dropped; the rest run as if they were the latest due ticks.
        m_timing.addMissed(static_cast<quint64>(due - maxCatchUp));
        m_nextDueNs += (due - maxCatchUp) * m_intervalNs;
    }

    const int run = static_cast<int>(std::min<qint64>(due, maxCatchUp));
    for (int i = 0; i < run; ++i) {
        m_timing.record(nowNs - m_nextDueNs);
        m_nextDueNs += m_intervalNs;
    }
    return run;
}

qint64 QcTickPacer::untilNextNs(qint64 nowNs) const
{
    return std::max<qint64>(m_nextDueNs - nowNs, 0);
}
