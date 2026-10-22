#include "server/admin/QcAdminStatusRecorder.h"

#include <algorithm>
#include <cmath>

namespace {

// The 99th percentile of `values` (nearest rank); 0 without values.
double percentile99(QList<double> values)
{
    if (values.isEmpty()) {
        return 0.0;
    }
    const auto rank = qsizetype(std::ceil(0.99 * double(values.size()))) - 1;
    const auto nth = values.begin() + std::clamp<qsizetype>(rank, 0, values.size() - 1);
    std::nth_element(values.begin(), nth, values.end());
    return *nth;
}

// Bytes per second over `seconds` in KiB, 0 when the counter went back (a restart of the counts).
float rate(quint64 now, quint64 before, double seconds)
{
    return now >= before && seconds > 0.0 ? float(double(now - before) / 1024.0 / seconds) : 0.0f;
}

} // namespace

void QcAdminStatusRecorder::noteTick(double ms)
{
    if (m_ticks.size() < ticksKept) {
        m_ticks.append(ms);
    }
    ++m_tickCount;
    m_tickSum += ms;
    m_tickMax = std::max(m_tickMax, ms);
}

QcMsg::AdminSample QcAdminStatusRecorder::sample(const Counts &counts, double seconds)
{
    QcMsg::AdminSample sample;
    sample.timeMs = counts.timeMs;
    sample.tickMeanMs = m_tickCount > 0 ? float(m_tickSum / double(m_tickCount)) : 0.0f;
    sample.tickP99Ms = float(percentile99(m_ticks));
    sample.tickMaxMs = float(m_tickMax);
    sample.ticksPerSecond = seconds > 0.0 ? float(double(m_tickCount) / seconds) : 0.0f;
    sample.players = counts.players;
    sample.loadedChunks = counts.loadedChunks;
    sample.entities = counts.entities;
    sample.memoryMb = counts.memoryMb;
    if (m_previous) {
        sample.sentKbPerSecond = rate(counts.sentBytes, m_previous->sentBytes, seconds);
        sample.receivedKbPerSecond = rate(counts.receivedBytes, m_previous->receivedBytes, seconds);
    }
    m_previous = counts;
    m_ticks.clear();
    m_tickCount = 0;
    m_tickSum = 0.0;
    m_tickMax = 0.0;

    if (m_seconds.size() >= secondsKept) {
        m_seconds.removeFirst();
    }
    m_seconds.append(sample);
    if (++m_inMinute >= 60) {
        foldMinute();
    }
    return sample;
}

std::optional<QcMsg::AdminSample> QcAdminStatusRecorder::latest() const
{
    return m_seconds.isEmpty() ? std::nullopt : std::optional(m_seconds.last());
}

void QcAdminStatusRecorder::foldMinute()
{
    const QList<QcMsg::AdminSample> minute = m_seconds.mid(m_seconds.size() - m_inMinute);
    m_inMinute = 0;
    QcMsg::AdminSample folded = minute.last();
    const auto mean = [&minute](float QcMsg::AdminSample::*field) {
        double sum = 0.0;
        for (const QcMsg::AdminSample &sample : minute) {
            sum += double(sample.*field);
        }
        return float(sum / double(minute.size()));
    };
    const auto highest = [&minute](float QcMsg::AdminSample::*field) {
        return std::ranges::max(minute, {}, field).*field;
    };
    folded.tickMeanMs = mean(&QcMsg::AdminSample::tickMeanMs);
    folded.ticksPerSecond = mean(&QcMsg::AdminSample::ticksPerSecond);
    folded.sentKbPerSecond = mean(&QcMsg::AdminSample::sentKbPerSecond);
    folded.receivedKbPerSecond = mean(&QcMsg::AdminSample::receivedKbPerSecond);
    folded.tickP99Ms = highest(&QcMsg::AdminSample::tickP99Ms);
    folded.tickMaxMs = highest(&QcMsg::AdminSample::tickMaxMs);

    if (m_minutes.size() >= minutesKept) {
        m_minutes.removeFirst();
    }
    m_minutes.append(folded);
}
