#pragma once

#include <QtTypes>

#include <deque>
#include <optional>

// Chooses the moment of server time at which the client shows other players and creatures
// (decision E-271). Every EntityMoves carries the server's time of its tick; the clock pairs it
// with the local time it arrived. The fastest arrival of the last two seconds (the smallest lag of
// local behind server time) is the best estimate of "now" on the server; the client shows
// `delayMs()` before that, so snapshots that come late by up to that much minus a tick are already
// there when they are needed. The shown moment does not jump when the estimate moves with other
// arrivals: the shown time runs at most 10 % faster or slower until it caught up. A change of more
// than a second, or of the snapshot interval by a factor of two or more (another tick rate), is
// taken at once (decision E-277).
class QcSnapshotClock
{
public:
    // The shown moment lies at least this far behind the newest snapshot that could have arrived.
    static constexpr double baseDelayMs = 100.0;
    // Lateness covered on top of the interval between two snapshots: delay = max(base, interval + this).
    static constexpr double jitterAllowanceMs = 50.0;
    static constexpr double windowMs = 2000.0;
    static constexpr double maxSlew = 0.1;
    static constexpr double snapMs = 1000.0;
    static constexpr double defaultIntervalMs = 50.0;
    static constexpr double maxIntervalMs = 1000.0;
    // An interval estimate this many times larger or smaller snaps the shown time.
    static constexpr double snapIntervalFactor = 2.0;
    // Arrivals kept at most, whatever the window holds (a server sending many messages per tick).
    static constexpr std::size_t maxArrivals = 256;

    // A snapshot of server time `serverMs` arrived at local time `localMs`.
    void observe(qint64 serverMs, qint64 localMs);
    // Moves the shown moment on to local time `localMs` (once per client tick) and returns the
    // server time to show; std::nullopt before the first snapshot.
    std::optional<double> advance(qint64 localMs);
    // The server's time stood still while the local time went on (a pause, decision E-877): the next
    // snapshot starts a new history, so the shown moment neither runs ahead of it nor waits out the
    // old lag.
    void restart()
    {
        m_arrivals.clear();
        m_shownLagMs.reset();
    }

    // The newest server time that arrived (-1 before the first).
    qint64 latestServerMs() const { return m_latestServerMs; }
    // The time between two snapshots (the server's tick interval, 50 ms at 20 ticks per second).
    double intervalMs() const { return m_intervalMs; }
    double delayMs() const;
    std::size_t arrivalCount() const { return m_arrivals.size(); }

private:
    struct Arrival
    {
        qint64 localMs = 0;
        qint64 serverMs = 0;
    };

    void updateEstimates();

    std::deque<Arrival> m_arrivals;
    qint64 m_latestServerMs = -1;
    double m_intervalMs = defaultIntervalMs;
    // Local minus server time of the fastest recent arrival.
    double m_fastestLagMs = 0.0;
    // Local minus shown server time, slewed towards m_fastestLagMs + delayMs().
    std::optional<double> m_shownLagMs;
    qint64 m_lastAdvanceMs = 0;
};
