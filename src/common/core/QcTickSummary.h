// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcTickPhase.h"

#include <QtGlobal>

#include <array>

/*! \brief Server tick durations, lateness and skipped ticks so far (QcTickStats::summary,
    benchmark). */
struct QcTickSummary
{
    /*! The mean and the longest tick duration. \qcshared */
    double averageMs = 0.0;
    double worstMs = 0.0;
    /*! Ticks recorded. */
    quint64 ticks = 0;
    /*! Start of a tick minus its due time: mean, 99th percentile and maximum. \qcshared */
    double lateAverageMs = 0.0;
    double lateP99Ms = 0.0;
    double lateMaxMs = 0.0;
    /*! Ticks dropped because the server fell too far behind. */
    quint64 skipped = 0;
    /*! Mean and longest time per phase (QcTickPhase) over the ticks that reported phases.
        \qcshared */
    std::array<double, QcTickPhases::count> phaseAverageMs{};
    std::array<double, QcTickPhases::count> phaseWorstMs{};
};
