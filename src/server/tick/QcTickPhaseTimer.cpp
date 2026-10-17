#include "server/tick/QcTickPhaseTimer.h"

#include <algorithm>
#include <chrono>

qint64 QcTickPhaseTimer::steadyNow()
{
    return qint64(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

QcTickPhaseTimer::QcTickPhaseTimer(Clock clock)
    : m_clock(clock)
    , m_window(seriesCount, windowTicks)
{
}

void QcTickPhaseTimer::beginTick()
{
    m_current.fill(0);
    m_lapStart = m_clock();
}

void QcTickPhaseTimer::lap(QcTickPhase phase)
{
    const qint64 now = m_clock();
    m_current[std::size_t(phase)] += now - m_lapStart;
    m_lapStart = now;
}

void QcTickPhaseTimer::endTick(qint64 lateNs)
{
    m_current[std::size_t(QcTickPhase::Messages)] = m_messagesNs;
    m_messagesNs = 0;
    m_lastTick = m_current;

    std::array<float, seriesCount> sample{};
    qint64 durationNs = 0;
    for (int phase = 0; phase < QcTickPhases::count; ++phase) {
        sample[std::size_t(phase)] = float(double(m_current[std::size_t(phase)]) / 1e6);
        durationNs += phase < QcTickPhases::inTick ? m_current[std::size_t(phase)] : 0;
    }
    sample[durationSeries] = float(double(durationNs) / 1e6);
    sample[lateSeries] = float(double(std::max<qint64>(lateNs, 0)) / 1e6);
    m_window.add(sample);
}
