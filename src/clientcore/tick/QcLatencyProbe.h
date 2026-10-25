#pragma once

#include "common/diagnostics/QcRoundTripStats.h"
#include "common/protocol/QcSessionMessages.h"

#include <QElapsedTimer>

#include <functional>
#include <mutex>
#include <optional>

// The client's measurement of the round trip to the server (decision E-886): every half second a
// LatencyProbe, answered by the server at once with a LatencyEcho; the round trips go into a
// QcRoundTripStats of the last ten seconds. Times come from its own steady clock (a QElapsedTimer, or
// the clock a test hands in), not from the session's tick clock, which tests replace. Probes and
// echoes come from the thread that ticks the session (QcSessionCore), summary() from any thread;
// every access takes the mutex.
class QcLatencyProbe
{
public:
    static constexpr qint64 intervalNs = 500'000'000;
    // A probe without an echo for this long counts as lost; the next one goes out.
    static constexpr qint64 lostAfterNs = 5'000'000'000;

    // Nanoseconds of a steady clock.
    using Clock = std::function<qint64()>;

    // Without a clock the probe measures with its own QElapsedTimer.
    explicit QcLatencyProbe(Clock clock = {});

    // The probe to send now, or std::nullopt while the last one is not due or still on its way.
    [[nodiscard]] std::optional<QcMsg::LatencyProbe> due();
    // The server answered the probe `nonce`; an echo of an older or lost probe is ignored.
    void echoed(quint32 nonce);
    // The round trips so far, with the age of a late probe (QcRoundTripSummary::pendingMs);
    // std::nullopt before the first echo.
    [[nodiscard]] std::optional<QcRoundTripSummary> summary() const;
    // Forgets the round trips so far; the echo of a probe on its way is ignored, the next probe
    // follows at the usual interval.
    void reset();

private:
    mutable std::mutex m_mutex;
    QElapsedTimer m_timer;
    Clock m_clock;
    QcRoundTripStats m_stats;
    quint32 m_nonce = 0;
    qint64 m_sentNs = 0;
    bool m_waiting = false;
    bool m_sentAny = false;
};
