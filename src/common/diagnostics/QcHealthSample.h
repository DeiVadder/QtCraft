#pragma once

#include "common/diagnostics/QcIntervalStats.h"
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
    qint64 loadedChunks = -1;
    qint64 entities = -1;
    qint64 players = -1;
    // Bytes so far over the network; the line shows the rate since the previous line.
    std::optional<QcTraffic> traffic;
};
