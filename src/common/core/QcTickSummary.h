#pragma once

#include "common/core/QcTickPhase.h"

#include <QtGlobal>

#include <array>

// Server tick durations, lateness and skipped ticks so far (QcTickStats::summary, benchmark).
struct QcTickSummary
{
    double averageMs = 0.0;
    double worstMs = 0.0;
    quint64 ticks = 0;
    // Start of a tick minus its due time.
    double lateAverageMs = 0.0;
    double lateP99Ms = 0.0;
    double lateMaxMs = 0.0;
    // Ticks dropped because the server fell too far behind.
    quint64 skipped = 0;
    // Mean and longest time per phase (QcTickPhase) over the ticks that reported phases.
    std::array<double, QcTickPhases::count> phaseAverageMs{};
    std::array<double, QcTickPhases::count> phaseWorstMs{};
};
