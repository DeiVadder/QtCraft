#pragma once

#include "QcTickStats.h"

#include <QtGlobal>

#include <functional>

// Deadline-based tick schedule (decision E-090): tick n is due at start + n * 50 ms on a monotonic
// clock, so late timers never shift the grid. Due ticks are caught up, at most maxCatchUpTicks in a
// row; an older backlog is dropped (logged) so an overloaded server cannot fall ever further behind.
// Not thread-safe: lives in the server's thread; only the stats may be read from elsewhere.
class QcTickScheduler
{
public:
    // Monotonic time in nanoseconds.
    using Clock = std::function<qint64()>;

    static constexpr qint64 intervalNs = 50'000'000;
    static constexpr int maxCatchUpTicks = 5;
    // How much earlier than due the event loop's timer is armed: on macOS it fires 1-3 ms after the
    // requested time (millisecond poll plus timer coalescing), so the rest is slept (untilDueNs).
    static constexpr qint64 timerLeadNs = 3'000'000;

    explicit QcTickScheduler(Clock clock = steadyClock());

    static Clock steadyClock();

    // The first tick is due one interval from now.
    void start();
    // Runs the due ticks (see the class comment) and returns the nanoseconds until the next one is
    // due (0 when it already is).
    qint64 runDue(const std::function<void()> &tick);

    qint64 nextDueNs() const { return m_nextDueNs; }
    // How late the tick that runDue() is running now started against its due time; 0 outside.
    qint64 currentLateNs() const { return m_currentLateNs; }
    // Nanoseconds until the next tick is due; negative when it is overdue.
    qint64 untilDueNs() const { return m_nextDueNs - m_clock(); }
    QcTickStats &stats() { return m_stats; }
    const QcTickStats &stats() const { return m_stats; }

private:
    // Drops the backlog beyond maxCatchUpTicks; returns the number of ticks to run now.
    int takeDueTicks(qint64 now);

    Clock m_clock;
    qint64 m_nextDueNs = 0;
    qint64 m_currentLateNs = 0;
    QcTickStats m_stats;
};
