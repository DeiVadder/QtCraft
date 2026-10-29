// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/tick/QcLatencyProbe.h"

#include <utility>

QcLatencyProbe::QcLatencyProbe(Clock clock)
    : m_clock(std::move(clock))
{
    if (!m_clock) {
        m_timer.start();
        m_clock = [this] { return m_timer.nsecsElapsed(); };
    }
}

std::optional<QcMsg::LatencyProbe> QcLatencyProbe::due()
{
    const std::scoped_lock lock(m_mutex);
    const qint64 now = m_clock();
    const qint64 wait = m_waiting ? lostAfterNs : intervalNs;
    if (m_sentAny && now - m_sentNs < wait) {
        return std::nullopt;
    }

    ++m_nonce;
    m_sentNs = now;
    m_waiting = true;
    m_sentAny = true;
    // Reported at display resolution, so the server's player list changes only when its text does.
    const std::optional<QcRoundTripSummary> known = m_stats.summary(now);
    return QcMsg::LatencyProbe{m_nonce, known ? QcListedLatency::quantizedUs(known->smoothedMs) : -1};
}

void QcLatencyProbe::echoed(quint32 nonce)
{
    const std::scoped_lock lock(m_mutex);
    if (!m_waiting || nonce != m_nonce) {
        return;
    }

    const qint64 now = m_clock();
    m_waiting = false;
    m_stats.add(now, now - m_sentNs);
}

void QcLatencyProbe::reset()
{
    const std::scoped_lock lock(m_mutex);
    m_stats = QcRoundTripStats();
    m_waiting = false;
}

std::optional<QcRoundTripSummary> QcLatencyProbe::summary() const
{
    const std::scoped_lock lock(m_mutex);
    const qint64 now = m_clock();
    std::optional<QcRoundTripSummary> result = m_stats.summary(now);
    if (result && m_waiting) {
        const double ageMs = static_cast<double>(now - m_sentNs) / 1e6;
        result->pendingMs = ageMs > result->smoothedMs ? ageMs : 0.0;
    }
    return result;
}
