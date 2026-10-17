#include "app/hooks/QcThreadCpuClock.h"

#if defined(Q_OS_UNIX)
#include <ctime>
#endif

qint64 QcThreadCpuClock::nowNs()
{
#if defined(Q_OS_UNIX) && defined(CLOCK_THREAD_CPUTIME_ID)
    timespec time{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time) != 0) {
        return -1;
    }
    return qint64(time.tv_sec) * 1'000'000'000 + qint64(time.tv_nsec);
#else
    return -1;
#endif
}
