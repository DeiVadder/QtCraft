// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/diagnostics/QcThreadCpuClock.h"

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(Q_OS_UNIX)
#include <ctime>
#endif

qint64 QcThreadCpuClock::nowNs()
{
#if defined(Q_OS_WIN)
    // Kernel and user time of the thread in units of 100 ns.
    FILETIME created{};
    FILETIME exited{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    if (!GetThreadTimes(GetCurrentThread(), &created, &exited, &kernelTime, &userTime)) {
        return -1;
    }
    const auto ticks = [](const FILETIME &value) {
        return (static_cast<qint64>(value.dwHighDateTime) << 32) | static_cast<qint64>(value.dwLowDateTime);
    };
    return (ticks(kernelTime) + ticks(userTime)) * 100;
#elif defined(Q_OS_UNIX) && defined(CLOCK_THREAD_CPUTIME_ID)
    timespec time{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time) != 0) {
        return -1;
    }
    return static_cast<qint64>(time.tv_sec) * 1'000'000'000 + static_cast<qint64>(time.tv_nsec);
#else
    return -1;
#endif
}

qint64 QcThreadCpuClock::frameNowNs()
{
    return fineGrained() ? nowNs() : -1;
}
