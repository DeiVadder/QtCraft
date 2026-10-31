// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcTickPhase.h"
#include "common/core/QcTickSummary.h"

#include <QtGlobal>

#include <array>

#include <mutex>
#include <vector>

/*! \brief Timing of the server ticks: how long they ran, how late they started against their due
    time on the tick grid (QcTickScheduler), and how many were skipped.

    Written by the server thread, read and reset by others (benchmark); every access takes the
    mutex. */
class QcTickStats
{
public:
    /*! Lateness samples kept for the percentile (the latest ones; about 55 minutes of ticks). */
    static constexpr qsizetype maxSamples = qsizetype{1} << 16;

    /*! The summary of the ticks so far. */
    using Summary = QcTickSummary;

    /*! Records a tick that started \a lateNs after it was due and ran for \a durationNs. */
    void record(qint64 lateNs, qint64 durationNs);
    /*! Counts \a count ticks dropped from a backlog. */
    void addSkipped(quint64 count);
    /*! Adds \a phaseNs, the nanoseconds per phase of one tick (QcTickPhaseTimer::lastTick). */
    void addPhases(const std::array<qint64, QcTickPhases::count> &phaseNs);
    /*! Returns the summary of the ticks recorded since the last reset. */
    Summary summary() const;
    /*! Returns the ticks dropped so far (cheap, unlike summary()). */
    quint64 skipped() const;
    /*! Forgets everything recorded. */
    void reset();

private:
    // The server thread hands the samples to readers on other threads under this mutex.
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
