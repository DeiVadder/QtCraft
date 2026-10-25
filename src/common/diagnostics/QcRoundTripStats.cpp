#include "common/diagnostics/QcRoundTripStats.h"

#include "common/protocol/QcSessionMessages.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace {

double toMs(qint64 ns)
{
    return static_cast<double>(ns) / 1e6;
}

} // namespace

QString QcRoundTripSummary::text(double ms)
{
    return QString::number(ms, 'f', ms < 10.0 ? 1 : 0);
}

qint32 QcListedLatency::quantizedUs(double ms)
{
    const double stepUs = ms < 10.0 ? 100.0 : 1000.0;
    const double us = std::round(std::max(ms, 0.0) * 1000.0 / stepUs) * stepUs;
    return static_cast<qint32>(std::min(us, static_cast<double>(QcMsg::LatencyProbe::maxReportedUs)));
}

int QcListedLatency::bars(qint32 latencyUs)
{
    if (latencyUs < 0) {
        return 0;
    }

    static constexpr std::array<qint32, 4> limitsUs{150'000, 300'000, 600'000, 1'000'000};
    const auto below = std::ranges::find_if(limitsUs, [latencyUs](qint32 limit) { return latencyUs < limit; });
    return 5 - static_cast<int>(below - limitsUs.begin());
}

bool QcListedLatency::changed(qint32 listedUs, qint32 reportedUs)
{
    if ((listedUs < 0) != (reportedUs < 0) || bars(listedUs) != bars(reportedUs)) {
        return true;
    }
    if (listedUs < 0) {
        return false;
    }

    static constexpr qint32 smallestStepUs = 1000;
    const qint32 stepUs = std::max(smallestStepUs, listedUs / 10);
    return std::abs(reportedUs - listedUs) >= stepUs;
}

void QcRoundTripStats::add(qint64 atNs, qint64 roundTripNs)
{
    const qint64 roundTrip = std::max<qint64>(roundTripNs, 0);
    m_samples.push_back({atNs, roundTrip});
    while (!m_samples.empty() && m_samples.front().atNs < atNs - m_windowNs) {
        m_samples.pop_front();
    }

    m_lastMs = toMs(roundTrip);
    m_smoothedMs = m_smoothedMs ? *m_smoothedMs + smoothing * (m_lastMs - *m_smoothedMs) : m_lastMs;
}

std::optional<QcRoundTripSummary> QcRoundTripStats::summary(qint64 nowNs) const
{
    if (!m_smoothedMs) {
        return std::nullopt;
    }

    QcRoundTripSummary result{m_lastMs, *m_smoothedMs, m_lastMs, m_lastMs, 0.0, 0};
    double jitterTotal = 0.0;
    std::optional<double> previous;
    for (const Sample &sample : m_samples) {
        if (sample.atNs < nowNs - m_windowNs) {
            continue;
        }

        const double ms = toMs(sample.roundTripNs);
        result.minMs = result.samples == 0 ? ms : std::min(result.minMs, ms);
        result.maxMs = result.samples == 0 ? ms : std::max(result.maxMs, ms);
        if (previous) {
            jitterTotal += std::abs(ms - *previous);
        }
        previous = ms;
        ++result.samples;
    }
    if (result.samples > 1) {
        result.jitterMs = jitterTotal / (result.samples - 1);
    }
    return result;
}
