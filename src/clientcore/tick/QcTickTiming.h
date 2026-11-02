// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

#include <mutex>
#include <vector>

/*! \brief Punctuality of the client session's ticks (decision E-560): how late each tick ran
    against its due time on the 50 ms grid, and how many due ticks never ran.

    Written by the thread that ticks the session, read and reset by others (benchmark, tests);
    every access takes the mutex. */
class QcTickTiming
{
public:
    /*! Lateness samples kept for the percentile (the latest ones; about 14 minutes of ticks). */
    static constexpr qsizetype maxSamples = qsizetype{1} << 14;

    /*! \brief The punctuality so far. */
    struct Summary
    {
        /*! Ticks run. */
        quint64 ticks = 0;
        /*! Due ticks that were dropped instead of run. */
        quint64 missed = 0;
        /*! Start of a tick minus its due time. */
        double lateAverageMs = 0.0;
        /*! The 99th percentile of the lateness over the samples kept. */
        double lateP99Ms = 0.0;
        /*! The largest lateness. */
        double lateMaxMs = 0.0;
    };

    /*! A tick ran \a lateNs nanoseconds after its due time. */
    void record(qint64 lateNs);
    /*! \a count due ticks were dropped. */
    void addMissed(quint64 count);
    /*! Returns the punctuality so far. */
    Summary summary() const;
    /*! Forgets the punctuality so far. */
    void reset();

private:
    // The session's tick thread hands the samples to readers on other threads under this mutex.
    mutable std::mutex m_mutex;
    std::vector<float> m_lateMs;
    qsizetype m_nextSample = 0;
    double m_lateTotalMs = 0.0;
    double m_lateMaxMs = 0.0;
    quint64 m_ticks = 0;
    quint64 m_missed = 0;
};
