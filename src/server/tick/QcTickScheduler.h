// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/tick/QcTickStats.h"
#include "server/tick/QcTimerResolution.h"

#include <QList>
#include <QtGlobal>

#include <functional>
#include <optional>

// Deadline-based tick schedule (decision E-090): tick n is due at start + n * interval on a
// monotonic clock, so late timers never shift the grid. Due ticks are caught up, at most
// maxCatchUpTicks in a row; an older backlog is dropped (logged) so an overloaded server cannot fall
// ever further behind. The interval follows the tick rate (/tick rate, decision E-249); a sprint
// runs a number of ticks back to back in slices of sprintSliceNs and then starts a new grid, so it
// leaves no backlog. tickRealNs() says how much real time a tick stands for, so what follows real
// time (the players' budgets and limits, autosaves, messages to the clients) keeps its pace at
// every rate and during a sprint. Not thread-safe: lives in the server's thread; only the stats may be read from
// elsewhere.
class QcTickScheduler
{
public:
    // Monotonic time in nanoseconds.
    using Clock = std::function<qint64()>;

    // The interval at the normal rate of 20 ticks per second.
    static constexpr qint64 intervalNs = 50'000'000;
    static constexpr double defaultRate = 20.0;
    static constexpr double minRate = 1.0;
    static constexpr double maxRate = 100.0;
    // A sprint returns to the event loop after this long, so messages are still handled.
    static constexpr qint64 sprintSliceNs = 20'000'000;
    static constexpr int maxCatchUpTicks = 5;
    // How much earlier than due the event loop's timer is armed: on macOS it fires 1-3 ms after the
    // requested time (millisecond poll plus timer coalescing), so the rest is slept (untilDueNs).
    static constexpr qint64 timerLeadNs = 3'000'000;

    explicit QcTickScheduler(Clock clock = steadyClock());

    static Clock steadyClock();

    // The first tick is due one interval from now. From start() to stop() the system's timers run
    // to the millisecond (QcTimerResolution, Windows).
    void start();
    void stop();
    // Runs the due ticks (see the class comment) and returns the nanoseconds until the next one is
    // due (0 when it already is).
    qint64 runDue(const std::function<void()> &tick);

    // Ticks per second (clamped to minRate..maxRate); the next tick is due one new interval from now.
    void setRate(double ticksPerSecond);
    double rate() const { return 1e9 / static_cast<double>(m_intervalNs); }
    qint64 currentIntervalNs() const { return m_intervalNs; }
    // Runs `ticks` ticks as fast as possible with the next runDue() calls; replaces a running sprint.
    void startSprint(quint64 ticks);
    // Called from the running tick when it did nothing (a paused server, decision E-877): it counts
    // neither in the stats nor as a tick of a sprint, and a sprint waits for the next due time.
    void skipCurrentTick() { m_tickSkipped = true; }
    // Ends a sprint early; the normal schedule starts one interval from now.
    void stopSprint();
    bool sprinting() const { return m_sprintRemaining > 0; }
    quint64 sprintRemaining() const { return m_sprintRemaining; }
    quint64 sprintDone() const { return m_sprintDone; }
    // A sprint that ended: the ticks it ran and its real time.
    struct SprintEnd
    {
        quint64 ticks = 0;
        qint64 elapsedNs = 0;
    };

    // Once after a sprint ended (finished or stopped), else nullopt.
    [[nodiscard]] std::optional<SprintEnd> takeSprintEnded();
    // Ticks per second of the running sprint so far (0 before its first tick).
    double sprintRate() const;
    // The real time the running tick stands for: one interval, during a sprint the real time since
    // the previous tick of the sprint began (at most one interval). Outside runDue() (tests run
    // ticks directly) one interval.
    qint64 tickRealNs() const { return m_sprintTickNs.value_or(m_intervalNs); }
    // Ticks per second over the latest ticks (up to measureTicks), 0 before two ticks ran.
    double measuredRate() const;

    qint64 nextDueNs() const { return m_nextDueNs; }
    // How late the tick that runDue() is running now started against its due time; 0 outside.
    qint64 currentLateNs() const { return m_currentLateNs; }
    // Nanoseconds until the next tick is due; negative when it is overdue.
    qint64 untilDueNs() const { return m_nextDueNs - m_clock(); }
    QcTickStats &stats() { return m_stats; }
    const QcTickStats &stats() const { return m_stats; }

private:
    static constexpr qsizetype measureTicks = 100;

    // Drops the backlog beyond maxCatchUpTicks; returns the number of ticks to run now.
    int takeDueTicks(qint64 now);
    qint64 runSprint(const std::function<void()> &tick);
    void endSprint();
    void noteTickStart(qint64 started);

    Clock m_clock;
    std::optional<QcTimerResolution> m_timerResolution;
    qint64 m_intervalNs = intervalNs;
    quint64 m_sprintRemaining = 0;
    quint64 m_sprintDone = 0;
    qint64 m_sprintStartedNs = 0;
    qint64 m_previousSprintTickNs = 0;
    // Set while a sprint tick runs (tickRealNs()).
    std::optional<qint64> m_sprintTickNs;
    bool m_sprintEnded = false;
    bool m_tickSkipped = false;
    // Start times of the latest ticks, oldest first.
    QList<qint64> m_tickStarts;
    qint64 m_nextDueNs = 0;
    qint64 m_currentLateNs = 0;
    QcTickStats m_stats;
};
