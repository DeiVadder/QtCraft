#pragma once

#include "clientcore/tick/QcTickTiming.h"

#include <QtGlobal>

// The due times of the client session's ticks (decision E-560): a fixed grid of one tick interval
// from the start, like the server's (QcTickScheduler, E-090). After a hold-up the ticks due since
// run back to back, at most maxCatchUp of them; older ones are dropped and the grid moves on, so a
// long stall does not turn into a burst of inputs the server would refuse. Plain logic on a given
// clock (nanoseconds) so tests drive it without waiting; records into its QcTickTiming. Used by
// one thread at a time; only timing() may be read from others.
class QcTickPacer
{
public:
    // Ticks run back to back after a hold-up; the server takes that many inputs at once (its input
    // budget is 10, QcServerClient::maxInputBudget).
    static constexpr int maxCatchUp = 5;

    explicit QcTickPacer(qint64 intervalNs);

    // The grid starts: the first tick is due one interval after `nowNs`.
    void start(qint64 nowNs);
    // The ticks to run at `nowNs` (0 to maxCatchUp), each recorded with its lateness; the due
    // ticks beyond those are counted as missed.
    int take(qint64 nowNs);
    // Nanoseconds from `nowNs` until the next tick is due (0 when it is due already).
    qint64 untilNextNs(qint64 nowNs) const;

    QcTickTiming &timing() { return m_timing; }
    const QcTickTiming &timing() const { return m_timing; }

private:
    qint64 m_intervalNs;
    qint64 m_nextDueNs = 0;
    QcTickTiming m_timing;
};
