// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/tick/QcTickPhaseTimer.h"

#include <QString>

#include <optional>

// Lag spikes in the server's log (decision E-834): a tick longer than 50 ms becomes a line with its
// duration and its longest phases. At most one line per ten seconds; the spikes in between are
// counted into the next line with the longest of them, so a slow phase cannot flood the log.
class QcLagReport
{
public:
    static constexpr qint64 thresholdNs = 50'000'000;
    static constexpr qint64 quietNs = 10'000'000'000;
    // Phases named in a line at most, the longest first.
    static constexpr int shownPhases = 5;

    // The line for the tick that ended at `nowNs` with these phase durations; std::nullopt when it
    // was no spike or a line was written less than quietNs before.
    std::optional<QString> check(const QcTickPhaseTimer::Durations &phases, qint64 nowNs);
    // The line for the spikes counted since the last line, at the end (server stop); std::nullopt
    // when there were none.
    std::optional<QString> flush();

private:
    QString line(const QcTickPhaseTimer::Durations &phases, qint64 tickNs) const;

    std::optional<qint64> m_lastLineNs;
    int m_suppressed = 0;
    qint64 m_suppressedWorstNs = 0;
};
