#include "QcProcessMemory.h"

#include <QtGlobal>

#if defined(Q_OS_UNIX)
#include <sys/resource.h>
#endif
#if defined(Q_OS_DARWIN)
#include <mach/mach.h>
#elif defined(Q_OS_LINUX)
#include <QFile>
#include <unistd.h>
#endif

namespace QcProcessMemory {

double currentMb()
{
#if defined(Q_OS_DARWIN)
    mach_task_basic_info_data_t info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
        return 0.0;
    }
    return double(info.resident_size) / (1024.0 * 1024.0);
#elif defined(Q_OS_LINUX)
    // Second field of statm: resident pages.
    QFile statm(QStringLiteral("/proc/self/statm"));
    if (!statm.open(QIODevice::ReadOnly)) {
        return 0.0;
    }
    const QList<QByteArray> fields = statm.readAll().split(' ');
    return fields.size() > 1 ? fields[1].toDouble() * double(sysconf(_SC_PAGESIZE)) / (1024.0 * 1024.0) : 0.0;
#else
    return 0.0;
#endif
}

double peakMb()
{
#if defined(Q_OS_UNIX)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) {
        return 0.0;
    }
#if defined(Q_OS_DARWIN)
    return double(usage.ru_maxrss) / (1024.0 * 1024.0); // bytes
#else
    return double(usage.ru_maxrss) / 1024.0; // kilobytes
#endif
#else
    return 0.0;
#endif
}

} // namespace QcProcessMemory
