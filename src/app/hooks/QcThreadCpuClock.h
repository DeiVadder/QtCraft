#pragma once

#include <QtGlobal>

// CPU time consumed by the calling thread, independent of waiting (VSync, locks, sleeping).
// Used by the benchmark to measure the cost of a frame below the display's refresh limit.
class QcThreadCpuClock
{
public:
    // Nanoseconds of CPU time of the calling thread, or -1 where the platform does not provide it.
    static qint64 nowNs();
};
