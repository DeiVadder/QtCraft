#pragma once

#include "common/core/QcPhaseWindow.h"
#include "common/core/QcTickPhase.h"

#include <QString>
#include <QtGlobal>

#include <array>
#include <atomic>
#include <optional>

// Permanent, cheap phase counters of the server tick (decision E-225): one monotonic clock read per
// phase, no allocation per tick. The time since the previous lap goes to the phase named by lap(),
// so the phases of a tick add up to its duration; the time spent on client messages between ticks
// is collected separately. The latest ticks stay in a window (mean and maximum per phase, the tick's
// duration and lateness) for the debug display. Lives in the server's thread; not thread-safe, except
// stall(), which other threads may call (tick watchdog, decision E-834).
class QcTickPhaseTimer
{
public:
    // Monotonic time in nanoseconds; replaceable for tests.
    using Clock = qint64 (*)();
    static qint64 steadyNow();

    // Five seconds of ticks.
    static constexpr int windowTicks = 100;
    // Window series after the phases: the tick's duration (phases inside the tick) and its lateness.
    static constexpr int durationSeries = QcTickPhases::count;
    static constexpr int lateSeries = QcTickPhases::count + 1;
    static constexpr int seriesCount = QcTickPhases::count + 2;

    using Durations = std::array<qint64, QcTickPhases::count>;

    explicit QcTickPhaseTimer(Clock clock = steadyNow);

    qint64 now() const { return m_clock(); }
    void beginTick();
    // The time since beginTick() or the previous lap belongs to `phase` (a phase may get several laps).
    void lap(QcTickPhase phase);
    // Ends the tick that started `lateNs` after it was due: the message time collected since the
    // previous tick joins it, and the tick enters the window.
    void endTick(qint64 lateNs);
    // Time spent handling client messages outside the tick.
    void addMessageTime(qint64 ns) { m_messagesNs += ns; }

    // When the tick running now has run for longer than `limitNs`: a line with how long and the phase
    // that finished last (the next one hangs); std::nullopt between ticks or before the limit. Safe
    // from any thread.
    std::optional<QString> stall(qint64 limitNs) const;

    // Nanoseconds per phase of the last finished tick.
    const Durations &lastTick() const { return m_lastTick; }
    const QcPhaseWindow &window() const { return m_window; }

    // Measures the lifetime of a scope as message time.
    class MessageScope
    {
    public:
        explicit MessageScope(QcTickPhaseTimer &timer)
            : m_timer(timer)
            , m_started(timer.now())
        {
        }
        ~MessageScope() { m_timer.addMessageTime(m_timer.now() - m_started); }
        Q_DISABLE_COPY_MOVE(MessageScope)

    private:
        QcTickPhaseTimer &m_timer;
        qint64 m_started = 0;
    };

private:
    Clock m_clock;
    qint64 m_lapStart = 0;
    qint64 m_messagesNs = 0;
    Durations m_current{};
    Durations m_lastTick{};
    QcPhaseWindow m_window;
    // For stall(): the start of the tick running now (-1 between ticks) and its phase that finished
    // last (-1 none yet).
    std::atomic<qint64> m_runningSinceNs{-1};
    std::atomic<int> m_finishedPhase{-1};
};
