// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/diagnostics/QcIntervalStats.h"

#include <algorithm>

void QcIntervalStats::add(double ms)
{
    m_totalMs += ms;
    m_maxMs = std::max(m_maxMs, ms);
    ++m_count;
}

std::optional<QcIntervalTiming> QcIntervalStats::take()
{
    if (m_count == 0) {
        return std::nullopt;
    }
    const QcIntervalTiming timing{m_totalMs / static_cast<double>(m_count), m_maxMs, m_count};
    *this = {};
    return timing;
}
