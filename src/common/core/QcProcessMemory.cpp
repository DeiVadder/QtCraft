#include "common/core/QcProcessMemory.h"

#include <QtGlobal>

#include <optional>

#if defined(Q_OS_UNIX)
#include <sys/resource.h>
#endif
#if defined(Q_OS_DARWIN)
#include <mach/mach.h>
#elif defined(Q_OS_LINUX)
#include <QFile>
#include <unistd.h>
#elif defined(Q_OS_WIN)
// clang-format off: windows.h first, psapi.h needs it.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
// clang-format on
#endif

#if defined(Q_OS_WIN)
namespace {

// Working set now and its peak (decision E-834).
std::optional<PROCESS_MEMORY_COUNTERS> windowsCounters()
{
    PROCESS_MEMORY_COUNTERS counters{};
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        return std::nullopt;
    }
    return counters;
}

} // namespace
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
    return double(info.resident_size) / bytesPerMb;
#elif defined(Q_OS_LINUX)
    // Second field of statm: resident pages.
    QFile statm(QStringLiteral("/proc/self/statm"));
    if (!statm.open(QIODevice::ReadOnly)) {
        return 0.0;
    }
    const QList<QByteArray> fields = statm.readAll().split(' ');
    return fields.size() > 1 ? fields[1].toDouble() * double(sysconf(_SC_PAGESIZE)) / bytesPerMb : 0.0;
#elif defined(Q_OS_WIN)
    const std::optional<PROCESS_MEMORY_COUNTERS> counters = windowsCounters();
    return counters ? double(counters->WorkingSetSize) / bytesPerMb : 0.0;
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
    return double(usage.ru_maxrss) / bytesPerMb; // bytes
#else
    return double(usage.ru_maxrss) / 1024.0; // kilobytes
#endif
#elif defined(Q_OS_WIN)
    const std::optional<PROCESS_MEMORY_COUNTERS> counters = windowsCounters();
    return counters ? double(counters->PeakWorkingSetSize) / bytesPerMb : 0.0;
#else
    return 0.0;
#endif
}

#if defined(Q_OS_DARWIN)
namespace {

std::optional<task_vm_info_data_t> vmInfo()
{
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
        return std::nullopt;
    }
    return info;
}

} // namespace
#endif

double footprintMb()
{
#if defined(Q_OS_DARWIN)
    const std::optional<task_vm_info_data_t> info = vmInfo();
    return info ? double(info->phys_footprint) / bytesPerMb : 0.0;
#else
    return currentMb();
#endif
}

double peakFootprintMb()
{
#if defined(Q_OS_DARWIN)
    const std::optional<task_vm_info_data_t> info = vmInfo();
    return info ? double(info->ledger_phys_footprint_peak) / bytesPerMb : 0.0;
#else
    return peakMb();
#endif
}

} // namespace QcProcessMemory
