#pragma once

#include "common/diagnostics/QcIntervalStats.h"
#include "common/diagnostics/QcRoundTripStats.h"
#include "common/protocol/QcConnection.h"

#include <optional>

// What a program reports in its health line; unknown parts stay empty or -1.
struct QcHealthSample
{
    // Frame intervals of the game since the previous line.
    std::optional<QcIntervalTiming> frames;
    // Server tick durations (the dedicated server: since the previous line).
    std::optional<QcIntervalTiming> ticks;
    // Time from sending a player input until the server's answer acknowledges it (client).
    std::optional<QcIntervalTiming> roundTrip;
    // The round trip of the latency probes over the last ten seconds (client, decision E-886).
    std::optional<QcRoundTripSummary> latency;
    qint64 loadedChunks = -1;
    qint64 entities = -1;
    qint64 players = -1;
    // The server's tick loop pauses (decision E-877): the line says "paused".
    bool paused = false;
    // Bytes so far over the network; the line shows the rate since the previous line.
    std::optional<QcTraffic> traffic;
};
