// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

/*! \brief Memory of the running process in MiB (debug display, statistics log, health line,
    benchmark); 0 where the platform does not report it.

    Windows reports the working set. */
namespace QcProcessMemory {

/*! Bytes per MiB: every memory figure of the debug display ("MiB"), the statistics JSON (keys
    ending in "Mb") and the BENCH line (keys ending in "_mb") uses this unit (decision E-813). */
inline constexpr double bytesPerMb = 1024.0 * 1024.0;

/*! Resident memory now. */
double currentMb();
/*! Highest resident memory so far. */
double peakMb();
/*! Physical footprint now (macOS: what the Activity Monitor shows, with GPU memory the process
    owns that is not mapped into it; elsewhere the resident memory). */
double footprintMb();
/*! Highest physical footprint so far (macOS; elsewhere the highest resident memory). */
double peakFootprintMb();

} // namespace QcProcessMemory
