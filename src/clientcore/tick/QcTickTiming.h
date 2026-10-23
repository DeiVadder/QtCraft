#pragma once

#include <QtGlobal>

#include <mutex>
#include <vector>

// Punctuality of the client session's ticks (decision E-560): how late each tick ran against its
// due time on the 50 ms grid, and how many due ticks never ran. Written by the thread that ticks
// the session, read and reset by others (benchmark, tests); every access takes the mutex.
class QcTickTiming
{
public:
    // Lateness samples kept for the percentile (the latest ones; about 14 minutes of ticks).
    static constexpr qsizetype maxSamples = qsizetype{1} << 14;

    struct Summary
    {
        quint64 ticks = 0;
        // Due ticks that were dropped instead of run.
        quint64 missed = 0;
        // Start of a tick minus its due time.
        double lateAverageMs = 0.0;
        double lateP99Ms = 0.0;
        double lateMaxMs = 0.0;
    };

    void record(qint64 lateNs);
    void addMissed(quint64 count);
    Summary summary() const;
    void reset();

private:
    mutable std::mutex m_mutex;
    std::vector<float> m_lateMs;
    qsizetype m_nextSample = 0;
    double m_lateTotalMs = 0.0;
    double m_lateMaxMs = 0.0;
    quint64 m_ticks = 0;
    quint64 m_missed = 0;
};
