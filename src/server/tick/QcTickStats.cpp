#include "server/tick/QcTickStats.h"

#include <algorithm>

void QcTickStats::record(qint64 lateNs, qint64 durationNs)
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
    m_durationTotalNs += durationNs;
    m_durationWorstNs = std::max(m_durationWorstNs, durationNs);
    ++m_ticks;
}

void QcTickStats::addSkipped(quint64 count)
{
    const std::scoped_lock lock(m_mutex);
    m_skipped += count;
}

void QcTickStats::addPhases(const std::array<qint64, QcTickPhases::count> &phaseNs)
{
    const std::scoped_lock lock(m_mutex);
    for (std::size_t phase = 0; phase < phaseNs.size(); ++phase) {
        m_phaseTotalNs[phase] += phaseNs[phase];
        m_phaseWorstNs[phase] = std::max(m_phaseWorstNs[phase], phaseNs[phase]);
    }
    ++m_phaseTicks;
}

quint64 QcTickStats::skipped() const
{
    const std::scoped_lock lock(m_mutex);
    return m_skipped;
}

QcTickStats::Summary QcTickStats::summary() const
{
    std::vector<float> sorted;
    Summary result;
    {
        const std::scoped_lock lock(m_mutex);
        result.skipped = m_skipped;
        for (std::size_t phase = 0; m_phaseTicks > 0 && phase < m_phaseTotalNs.size(); ++phase) {
            result.phaseAverageMs[phase] = static_cast<double>(m_phaseTotalNs[phase]) / static_cast<double>(m_phaseTicks) / 1e6;
            result.phaseWorstMs[phase] = static_cast<double>(m_phaseWorstNs[phase]) / 1e6;
        }
        if (m_ticks == 0) {
            return result;
        }

        result.averageMs = static_cast<double>(m_durationTotalNs) / static_cast<double>(m_ticks) / 1e6;
        result.worstMs = static_cast<double>(m_durationWorstNs) / 1e6;
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
    m_phaseTotalNs.fill(0);
    m_phaseWorstNs.fill(0);
    m_phaseTicks = 0;
}
