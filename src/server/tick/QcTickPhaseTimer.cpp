#include "server/tick/QcTickPhaseTimer.h"

#include <algorithm>
#include <chrono>

using namespace Qt::StringLiterals;

qint64 QcTickPhaseTimer::steadyNow()
{
    return static_cast<qint64>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
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
    m_finishedPhase.store(-1, std::memory_order_relaxed);
    m_runningSinceNs.store(m_lapStart, std::memory_order_relaxed);
}

void QcTickPhaseTimer::lap(QcTickPhase phase)
{
    const qint64 now = m_clock();
    m_current[static_cast<std::size_t>(phase)] += now - m_lapStart;
    m_lapStart = now;
    m_finishedPhase.store(static_cast<int>(phase), std::memory_order_relaxed);
}

void QcTickPhaseTimer::endTick(qint64 lateNs)
{
    m_runningSinceNs.store(-1, std::memory_order_relaxed);
    m_current[static_cast<std::size_t>(QcTickPhase::Messages)] = m_messagesNs;
    m_messagesNs = 0;
    m_lastTick = m_current;

    std::array<float, seriesCount> sample{};
    qint64 durationNs = 0;
    for (int phase = 0; phase < QcTickPhases::count; ++phase) {
        sample[static_cast<std::size_t>(phase)] = static_cast<float>(static_cast<double>(m_current[static_cast<std::size_t>(phase)]) / 1e6);
        durationNs += phase < QcTickPhases::inTick ? m_current[static_cast<std::size_t>(phase)] : 0;
    }
    sample[durationSeries] = static_cast<float>(static_cast<double>(durationNs) / 1e6);
    sample[lateSeries] = static_cast<float>(static_cast<double>(std::max<qint64>(lateNs, 0)) / 1e6);
    m_window.add(sample);
}

std::optional<QString> QcTickPhaseTimer::stall(qint64 limitNs) const
{
    const qint64 since = m_runningSinceNs.load(std::memory_order_relaxed);
    const qint64 running = since < 0 ? 0 : m_clock() - since;
    if (running <= limitNs) {
        return std::nullopt;
    }
    const int phase = m_finishedPhase.load(std::memory_order_relaxed);
    const QString finished = phase < 0 ? u"none"_s : QString::fromLatin1(QcTickPhases::ids[static_cast<std::size_t>(phase)]);
    return u"Server tick running for %1 s, the last phase that finished: %2"_s.arg(running / 1'000'000'000).arg(finished);
}
