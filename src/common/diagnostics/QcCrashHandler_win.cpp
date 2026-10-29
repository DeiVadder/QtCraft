// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// The crash handler of Windows (decision E-834): an unhandled-exception filter that writes a
// minidump (dbghelp) and a text report with the exception, its module and offset, the thread and
// the return addresses of the stack; abort(), fatal Qt messages, pure virtual calls and invalid
// parameters of the C runtime are reported the same way without exception record. A stack overflow
// is reported from a helper thread (the crashed stack has no room left) with the crashed thread's id
// and instruction pointer; its stack is in the dump. The paths are prepared at install. The log gets
// no crash line here (QFile's handle is no Windows handle); the next start names the report.

#include "common/diagnostics/QcCrashHandler.h"

#include <array>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>

// clang-format off: windows.h first, dbghelp.h needs it.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
// clang-format on

namespace {

std::wstring g_reportPath;
std::wstring g_dumpPath;
std::string g_header;
bool g_fullDump = false;
volatile LONG g_handling = 0;
volatile LONG g_reported = 0;
// The thread that writes the report (0: none).
volatile DWORD g_reportingThread = 0;
// How long a second crashing thread waits for the report of the first.
constexpr DWORD waitForReportMs = 5000;
// Stack the handler keeps after a stack overflow (SetThreadStackGuarantee).
constexpr ULONG stackGuaranteeBytes = 64 * 1024;

// What crashed: the exception (none for abort() and the like), the thread it happened on and the
// cause the report names without exception record.
struct Crash
{
    EXCEPTION_POINTERS *exception = nullptr;
    DWORD thread = 0;
    const char *what = "";
};

const void *instructionPointer(const CONTEXT *context)
{
#if defined(_M_X64)
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): the register holds the faulting address
    return reinterpret_cast<const void *>(context->Rip);
#elif defined(_M_ARM64)
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): the register holds the faulting address
    return reinterpret_cast<const void *>(context->Pc);
#else
    Q_UNUSED(context);
    return nullptr;
#endif
}

void writeDump(const Crash &crash)
{
    const HANDLE file = CreateFileW(g_dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    // Not "small": windows.h defines it as a macro.
    const int stacks = MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules;
    const int whole = MiniDumpWithFullMemory | MiniDumpWithHandleData | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules;
    MINIDUMP_EXCEPTION_INFORMATION info{crash.thread, crash.exception, FALSE};
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MINIDUMP_TYPE(g_fullDump ? whole : stacks),
                      crash.exception ? &info : nullptr, nullptr, nullptr);
    CloseHandle(file);
}

void writeText(HANDLE file, const char *text)
{
    DWORD written = 0;
    WriteFile(file, text, static_cast<DWORD>(std::char_traits<char>::length(text)), &written, nullptr);
}

// "module+0xoffset" of a code address.
void writeAddress(HANDLE file, const void *address)
{
    std::array<char, MAX_PATH> module{};
    HMODULE handle = nullptr;
    const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if (GetModuleHandleExA(flags, static_cast<LPCSTR>(address), &handle) && GetModuleFileNameA(handle, module.data(), static_cast<DWORD>(module.size())) > 0) {
        std::array<char, MAX_PATH + 64> line{};
        std::snprintf(line.data(), line.size(), "%s+0x%llx\n", module.data(),
                      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast): offset of the address in its module
                      static_cast<unsigned long long>(reinterpret_cast<const char *>(address) - reinterpret_cast<const char *>(handle)));
        writeText(file, line.data());
        return;
    }
    std::array<char, 64> line{};
    std::snprintf(line.data(), line.size(), "0x%p\n", address);
    writeText(file, line.data());
}

// The return addresses of the calling thread, which crashed.
void writeStack(HANDLE file)
{
    writeText(file, "stack (return addresses; the minidump has the full stacks):\n");
    std::array<void *, 62> frames{};
    const USHORT count = CaptureStackBackTrace(0, static_cast<DWORD>(frames.size()), frames.data(), nullptr);
    for (USHORT i = 0; i < count; ++i) {
        writeAddress(file, frames[i]);
    }
}

bool isOverflow(const EXCEPTION_POINTERS *exception)
{
    return exception && exception->ExceptionRecord && exception->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW;
}

void writeReport(const Crash &crash)
{
    const HANDLE file = CreateFileW(g_reportPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }
    std::array<char, 256> line{};
    writeText(file, g_header.c_str());
    if (crash.exception && crash.exception->ExceptionRecord) {
        std::snprintf(line.data(), line.size(), "exception: 0x%08lx\naddress: ", crash.exception->ExceptionRecord->ExceptionCode);
        writeText(file, line.data());
        writeAddress(file, crash.exception->ExceptionRecord->ExceptionAddress);
    } else {
        std::snprintf(line.data(), line.size(), "exception: %s\n", crash.what);
        writeText(file, line.data());
    }
    std::snprintf(line.data(), line.size(), "thread: %lu\n", crash.thread);
    writeText(file, line.data());
    if (const char *reason = QcCrashHandler::reason(); reason[0] != '\0') {
        writeText(file, "reason: ");
        writeText(file, reason);
        writeText(file, "\n");
    }
    if (isOverflow(crash.exception)) {
        // Written by a helper thread: its own stack would say nothing about the crash.
        writeText(file, "instruction pointer: ");
        writeAddress(file, instructionPointer(crash.exception->ContextRecord));
        writeText(file, "stack: see the minidump (stack overflow)\n");
    } else {
        writeStack(file);
    }
    CloseHandle(file);
}

// Another thread writes the report: give it time (bounded) before this thread ends the process.
// The thread that writes and crashes again goes on at once.
void waitForReport()
{
    if (g_reportingThread == GetCurrentThreadId()) {
        return;
    }
    for (DWORD waited = 0; waited < waitForReportMs && g_reported == 0; waited += 10) {
        Sleep(10);
    }
}

void report(const Crash &crash)
{
    if (InterlockedExchange(&g_handling, 1) != 0) {
        waitForReport();
        return;
    }
    g_reportingThread = GetCurrentThreadId();
    writeDump(crash);
    writeReport(crash);
    InterlockedExchange(&g_reported, 1);
}

void reportHere(const char *what)
{
    report({nullptr, GetCurrentThreadId(), what});
}

// The report of a stack overflow on a thread of its own: the crashed stack has no room left.
DWORD WINAPI reportOverflow(void *crash)
{
    report(*static_cast<const Crash *>(crash));
    return 0;
}

LONG WINAPI onException(EXCEPTION_POINTERS *exception)
{
    Crash crash{exception, GetCurrentThreadId(), "unhandled exception"};
    if (const HANDLE helper = isOverflow(exception) ? CreateThread(nullptr, 256 * 1024, reportOverflow, &crash, 0, nullptr) : nullptr) {
        WaitForSingleObject(helper, INFINITE);
        CloseHandle(helper);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    report(crash);
    // Windows Error Reporting and the debugger still see the crash.
    return EXCEPTION_CONTINUE_SEARCH;
}

void onAbort(int /*signal*/)
{
    reportHere("abort");
    _exit(3);
}

void onPureCall()
{
    reportHere("pure virtual function call");
    _exit(3);
}

void onInvalidParameter(const wchar_t *, const wchar_t *, const wchar_t *, unsigned int, uintptr_t)
{
    reportHere("invalid parameter to the C runtime");
    _exit(3);
}

} // namespace

namespace QcCrashHandler {

bool installPlatform(const Setup &setup)
{
    g_reportPath = setup.reportPath.toStdWString();
    g_dumpPath = setup.dumpPath.toStdWString();
    g_header = setup.header.toStdString();
    g_fullDump = setup.fullDump;

    // Replaces an earlier filter without keeping it (see QcCrashHandler.h).
    SetUnhandledExceptionFilter(onException);
    preparePlatformThread();
    std::signal(SIGABRT, onAbort);
    // No message box and no second report from the runtime on abort().
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _set_purecall_handler(onPureCall);
    _set_invalid_parameter_handler(onInvalidParameter);
    return true;
}

void preparePlatformCrashTest()
{
    // No "stopped working" dialog of Windows Error Reporting: a test waits for the process to end.
    SetErrorMode(GetErrorMode() | SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
}

void reportFatalMessage()
{
    reportHere("fatal message");
}

void preparePlatformThread()
{
    // Room for the filter after a stack overflow of this thread (it starts the helper thread).
    ULONG guarantee = stackGuaranteeBytes;
    SetThreadStackGuarantee(&guarantee);
}

quint64 threadId()
{
    return GetCurrentThreadId();
}

} // namespace QcCrashHandler
