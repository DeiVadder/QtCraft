#include "QcTickStats.h"

#include <algorithm>

void QcTickStats::record(qint64 lateNs, qint64 durationNs)
{
    const double lateMs = double(std::max<qint64>(lateNs, 0)) / 1e6;
    const std::scoped_lock lock(m_mutex);

    if (qsizetype(m_lateMs.size()) < maxSamples) {
        m_lateMs.push_back(float(lateMs));
    } else {
        m_lateMs[size_t(m_nextSample)] = float(lateMs);
        m_nextSample = (m_nextSample + 1) % maxSamples;
    }

    m_lateTotalMs += lateMs;
    m_lateMaxMs = std::max(m_lateMaxMs, lateMs);
    m_durationTotalNs += durationNs;
    m_durationWorstNs = std::max(m_durationWorstNs, durationNs);
    ++m_ticks;
}

void QcTickStats::addSkipped(quint64 count)
{
    const std::scoped_lock lock(m_mutex);
    m_skipped += count;
}

QcTickStats::Summary QcTickStats::summary() const
{
    std::vector<float> sorted;
    Summary result;
    {
        const std::scoped_lock lock(m_mutex);
        result.skipped = m_skipped;
        if (m_ticks == 0) {
            return result;
        }

        result.averageMs = double(m_durationTotalNs) / double(m_ticks) / 1e6;
        result.worstMs = double(m_durationWorstNs) / 1e6;
        result.ticks = m_ticks;
        result.lateAverageMs = m_lateTotalMs / double(m_ticks);
        result.lateMaxMs = m_lateMaxMs;
        sorted = m_lateMs;
    }

    // Nearest-rank percentile over the kept samples: the ceil(0.99 n)-th smallest.
    const size_t rank = (sorted.size() * 99 + 99) / 100 - 1;
    std::ranges::nth_element(sorted, sorted.begin() + qsizetype(rank));
    result.lateP99Ms = double(sorted[rank]);
    return result;
}

void QcTickStats::reset()
{
    const std::scoped_lock lock(m_mutex);
    m_lateMs.clear();
    m_nextSample = 0;
    m_lateTotalMs = 0.0;
    m_lateMaxMs = 0.0;
    m_durationTotalNs = 0;
    m_durationWorstNs = 0;
    m_ticks = 0;
    m_skipped = 0;
}
