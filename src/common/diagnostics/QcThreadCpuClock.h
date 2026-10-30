// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

/*! \brief CPU time consumed by the calling thread, independent of waiting (VSync, locks, sleeping).

    Used by the benchmark to measure the cost of a frame below the display's refresh limit, and
    by tests that compare the cost of two code paths over many chunks or ticks. */
class QcThreadCpuClock
{
public:
    /*! Nanoseconds of CPU time of the calling thread, or -1 where the platform does not provide
        it. POSIX: CLOCK_THREAD_CPUTIME_ID; Windows: GetThreadTimes (kernel plus user time),
        which advances in scheduler ticks of about 15.6 ms and so suits only long measurements. */
    static qint64 nowNs();

    /*! Returns whether nowNs() resolves far below a frame (true on POSIX). Per-frame
        measurements report "not provided" where it does not, instead of differences of 0 or 15.6
        ms (decision E-898). */
    static constexpr bool fineGrained()
    {
#if defined(Q_OS_WIN)
        return false;
#else
        return true;
#endif
    }

    /*! nowNs() where fineGrained(), otherwise -1: the reading for per-frame measurements.
        Defined out of line so callers do not test a value the compiler already knows (MSVC
        C4127). */
    static qint64 frameNowNs();
};
