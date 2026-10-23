#include "clientcore/tick/QcTickTiming.h"

#include <algorithm>

void QcTickTiming::record(qint64 lateNs)
{
    const double lateMs = static_cast<double>(std::max<qint64>(lateNs, 0)) / 1e6;
    const std::scoped_lock lock(m_mutex);

    if (static_cast<qsizetype>(m_lateMs.size()) < maxSamples) {
        m_lateMs.push_back(static_cast<float>(lateMs));
    } else {
        m_lateMs[static_cast<size_t>(m_nextSample)] = static_cast<float>(lateMs);
        m_nextSample = (m_nextSample + 1) % maxSamples;
    }

    m_lateTotalMs += lateMs;
    m_lateMaxMs = std::max(m_lateMaxMs, lateMs);
    ++m_ticks;
}

void QcTickTiming::addMissed(quint64 count)
{
    const std::scoped_lock lock(m_mutex);
    m_missed += count;
}

QcTickTiming::Summary QcTickTiming::summary() const
{
    std::vector<float> sorted;
    Summary result;
    {
        const std::scoped_lock lock(m_mutex);
        result.missed = m_missed;
        if (m_ticks == 0) {
            return result;
        }

        result.ticks = m_ticks;
        result.lateAverageMs = m_lateTotalMs / static_cast<double>(m_ticks);
        result.lateMaxMs = m_lateMaxMs;
        sorted = m_lateMs;
    }

    // Nearest-rank percentile over the kept samples: the ceil(0.99 n)-th smallest.
    const size_t rank = (sorted.size() * 99 + 99) / 100 - 1;
    std::ranges::nth_element(sorted, sorted.begin() + static_cast<qsizetype>(rank));
    result.lateP99Ms = static_cast<double>(sorted[rank]);
    return result;
}

void QcTickTiming::reset()
{
    const std::scoped_lock lock(m_mutex);
    m_lateMs.clear();
    m_nextSample = 0;
    m_lateTotalMs = 0.0;
    m_lateMaxMs = 0.0;
    m_ticks = 0;
    m_missed = 0;
}
