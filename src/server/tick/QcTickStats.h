// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcTickPhase.h"
#include "common/core/QcTickSummary.h"

#include <QtGlobal>

#include <array>

#include <mutex>
#include <vector>

// Timing of the server ticks: how long they ran, how late they started against their due time on
// the fixed 50 ms grid, and how many were skipped. Written by the server thread, read and reset by
// others (benchmark); every access takes the mutex.
class QcTickStats
{
public:
    // Lateness samples kept for the percentile (the latest ones; about 55 minutes of ticks).
    static constexpr qsizetype maxSamples = qsizetype{1} << 16;

    using Summary = QcTickSummary;

    void record(qint64 lateNs, qint64 durationNs);
    void addSkipped(quint64 count);
    // Nanoseconds per phase of one tick (QcTickPhaseTimer::lastTick).
    void addPhases(const std::array<qint64, QcTickPhases::count> &phaseNs);
    Summary summary() const;
    // Ticks dropped so far (cheap, unlike summary()).
    quint64 skipped() const;
    void reset();

private:
    mutable std::mutex m_mutex;
    std::vector<float> m_lateMs;
    qsizetype m_nextSample = 0;
    double m_lateTotalMs = 0.0;
    double m_lateMaxMs = 0.0;
    qint64 m_durationTotalNs = 0;
    qint64 m_durationWorstNs = 0;
    quint64 m_ticks = 0;
    quint64 m_skipped = 0;
    std::array<qint64, QcTickPhases::count> m_phaseTotalNs{};
    std::array<qint64, QcTickPhases::count> m_phaseWorstNs{};
    quint64 m_phaseTicks = 0;
};
