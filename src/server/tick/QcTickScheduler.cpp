#include "server/tick/QcTickScheduler.h"

#include <QLoggingCategory>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace {

Q_LOGGING_CATEGORY(lcTicks, "qtcraft.server")

} // namespace

QcTickScheduler::QcTickScheduler(Clock clock)
    : m_clock(std::move(clock))
{
}

QcTickScheduler::Clock QcTickScheduler::steadyClock()
{
    return [] {
        return qint64(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    };
}

void QcTickScheduler::start()
{
    m_nextDueNs = m_clock() + m_intervalNs;
}

void QcTickScheduler::setRate(double ticksPerSecond)
{
    m_intervalNs = qint64(std::llround(1e9 / std::clamp(ticksPerSecond, minRate, maxRate)));
    m_nextDueNs = m_clock() + m_intervalNs;
    m_tickStarts.clear();
}

void QcTickScheduler::startSprint(quint64 ticks)
{
    m_sprintRemaining = ticks;
    m_sprintDone = 0;
    m_sprintStartedNs = m_clock();
    m_previousSprintTickNs = m_sprintStartedNs;
    m_sprintEnded = false;
}

void QcTickScheduler::stopSprint()
{
    if (m_sprintRemaining > 0) {
        endSprint();
    }
}

void QcTickScheduler::endSprint()
{
    // A new grid: the time the sprint took is no backlog to catch up.
    m_sprintRemaining = 0;
    m_sprintEnded = true;
    m_nextDueNs = m_clock() + m_intervalNs;
    m_tickStarts.clear();
}

bool QcTickScheduler::takeSprintEnded(quint64 &ticks, qint64 &elapsedNs)
{
    if (!std::exchange(m_sprintEnded, false)) {
        return false;
    }
    ticks = m_sprintDone;
    elapsedNs = m_clock() - m_sprintStartedNs;
    return true;
}

void QcTickScheduler::noteTickStart(qint64 started)
{
    m_tickStarts.append(started);
    if (m_tickStarts.size() > measureTicks) {
        m_tickStarts.removeFirst();
    }
}

double QcTickScheduler::sprintRate() const
{
    const qint64 elapsed = m_clock() - m_sprintStartedNs;
    return m_sprintRemaining == 0 || m_sprintDone == 0 || elapsed <= 0 ? 0.0 : double(m_sprintDone) * 1e9 / double(elapsed);
}

double QcTickScheduler::measuredRate() const
{
    if (m_tickStarts.size() < 2 || m_tickStarts.last() <= m_tickStarts.first()) {
        return 0.0;
    }
    return double(m_tickStarts.size() - 1) * 1e9 / double(m_tickStarts.last() - m_tickStarts.first());
}

qint64 QcTickScheduler::runDue(const std::function<void()> &tick)
{
    if (m_sprintRemaining > 0) {
        return runSprint(tick);
    }

    const int count = takeDueTicks(m_clock());
    for (int i = 0; i < count; ++i) {
        const qint64 started = m_clock();
        noteTickStart(started);
        m_currentLateNs = started - m_nextDueNs;
        tick();
        m_stats.record(m_currentLateNs, m_clock() - started);
        m_nextDueNs += m_intervalNs;
        m_currentLateNs = 0;
    }

    return std::max<qint64>(0, m_nextDueNs - m_clock());
}

qint64 QcTickScheduler::runSprint(const std::function<void()> &tick)
{
    // Ticks run back to back, none counts as late; after a slice the event loop gets its turn.
    const qint64 sliceEnd = m_clock() + sprintSliceNs;
    while (m_sprintRemaining > 0 && m_clock() < sliceEnd) {
        const qint64 started = m_clock();
        noteTickStart(started);
        m_sprintTickNs = std::clamp<qint64>(started - m_previousSprintTickNs, 0, m_intervalNs);
        m_previousSprintTickNs = started;
        tick();
        m_sprintTickNs.reset();
        m_stats.record(0, m_clock() - started);
        --m_sprintRemaining;
        ++m_sprintDone;
    }

    if (m_sprintRemaining == 0) {
        endSprint();
        return m_intervalNs;
    }
    return 0;
}

int QcTickScheduler::takeDueTicks(qint64 now)
{
    if (now < m_nextDueNs) {
        return 0;
    }

    const qint64 due = (now - m_nextDueNs) / m_intervalNs + 1;
    if (due <= maxCatchUpTicks) {
        return int(due);
    }

    // The dropped ticks are lost game time; the grid moves on so no drift builds up.
    const qint64 skipped = due - maxCatchUpTicks;
    m_nextDueNs += skipped * m_intervalNs;
    m_stats.addSkipped(quint64(skipped));
    qCWarning(lcTicks) << "Server overloaded, skipped" << skipped << "ticks";
    return maxCatchUpTicks;
}
