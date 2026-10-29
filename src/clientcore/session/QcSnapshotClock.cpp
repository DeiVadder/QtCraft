// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcSnapshotClock.h"

#include <algorithm>
#include <cmath>
#include <limits>

void QcSnapshotClock::observe(qint64 serverMs, qint64 localMs)
{
    // A server that restarted its time (another world) starts a new history.
    if (serverMs < m_latestServerMs) {
        m_arrivals.clear();
        m_shownLagMs.reset();
    }

    m_latestServerMs = serverMs;
    m_arrivals.push_back({localMs, serverMs});
    while (m_arrivals.size() > 1 && (localMs - m_arrivals.front().localMs > windowMs || m_arrivals.size() > maxArrivals)) {
        m_arrivals.pop_front();
    }
    updateEstimates();
}

void QcSnapshotClock::updateEstimates()
{
    double fastest = std::numeric_limits<double>::max();
    double interval = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < m_arrivals.size(); ++i) {
        fastest = std::min(fastest, static_cast<double>(m_arrivals[i].localMs - m_arrivals[i].serverMs));
        if (i > 0 && m_arrivals[i].serverMs > m_arrivals[i - 1].serverMs) {
            interval = std::min(interval, static_cast<double>(m_arrivals[i].serverMs - m_arrivals[i - 1].serverMs));
        }
    }

    m_fastestLagMs = fastest;
    // Without two different times yet, assume the normal tick rate; the slowest rate is 1 per second.
    const double previous = m_intervalMs;
    m_intervalMs = interval == std::numeric_limits<double>::max() ? defaultIntervalMs : std::min(interval, maxIntervalMs);

    // Another tick rate (/tick rate): slewing 10 % towards a delay a second longer or shorter would
    // take ten seconds of stopping or rushing figures; it is taken at once.
    if (std::max(previous, m_intervalMs) >= snapIntervalFactor * std::min(previous, m_intervalMs)) {
        m_shownLagMs.reset();
    }
}

double QcSnapshotClock::delayMs() const
{
    return std::max(baseDelayMs, m_intervalMs + jitterAllowanceMs);
}

std::optional<double> QcSnapshotClock::advance(qint64 localMs)
{
    if (m_arrivals.empty()) {
        return std::nullopt;
    }

    const double wanted = m_fastestLagMs + delayMs();
    if (!m_shownLagMs || std::abs(wanted - *m_shownLagMs) > snapMs) {
        m_shownLagMs = wanted;
    } else {
        // The shown time runs between 0.9 and 1.1 times as fast as the local clock: no jumps.
        const double step = maxSlew * static_cast<double>(std::max<qint64>(0, localMs - m_lastAdvanceMs));
        *m_shownLagMs += std::clamp(wanted - *m_shownLagMs, -step, step);
    }

    m_lastAdvanceMs = localMs;
    return static_cast<double>(localMs) - *m_shownLagMs;
}
