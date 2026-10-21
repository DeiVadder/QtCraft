#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// Crash reports (decision E-834). Installed once at the start of a program: on a fatal signal
// (macOS, Linux: SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP, SIGSYS) or an unhandled exception
// (Windows) it writes a text report with the cause, address, thread and the stack, on Windows also a
// minidump, and (macOS, Linux) notes the crash at the end of the log file; then the process ends as
// it would have without it. std::terminate and qFatal end in abort() and are reported the same way,
// with their reason. The handler replaces earlier handlers of these signals and an earlier
// unhandled-exception filter without calling them (nothing in QtCraft installs one before).
// The signal handler uses only async-signal-safe calls on buffers prepared by install(). A second
// thread that crashes while the first writes its report waits for it (at most five seconds). Limits:
// a stack overflow is reported only on threads with an alternate signal stack (the main thread and
// those that called prepareThread()), and the symbol lookup of the backtrace may hang when the crash
// happened inside the dynamic loader. Builds with a sanitizer keep their own reports and install
// nothing.
namespace QcCrashHandler {

struct Setup
{
    QString reportPath;
    // Windows only: the minidump.
    QString dumpPath;
    // Windows only: the whole memory of the process in the dump (--full-dump), else the stacks and
    // the memory they point to.
    bool fullDump = false;
    // Lines at the top of the report (build, system).
    QByteArray header;
};

// Whether this build can install the handler (not with sanitizers).
bool available();
// Installs the handler; false (after a warning) where it cannot.
bool install(const Setup &setup);
// The reason the report names (a fatal message, a terminate); kept in a fixed buffer.
void setReason(const QByteArray &reason);
// The log file's handle (QcLogWriter::handle), -1 for none: the crash line goes there.
void setLogHandle(int handle);
// Prepares the calling thread for crash reports, at its start: an alternate signal stack of its
// own (macOS, Linux; so a stack overflow there is reported), room for the handler after a stack
// overflow and the terminate handler, which MSVC keeps per thread (Windows). Does nothing until
// install() succeeded.
void prepareThread();
// The id of the calling thread as crash reports and log lines name it (the system's thread id);
// async-signal-safe.
quint64 threadId();
// The test hook --crash-test: crashes this way at once ("segv", "abort", "fatal", "terminate",
// "trap", or "thread-segv" and "thread-overflow" on a thread of its own). Returns false for an
// unknown kind.
bool crashForTest(const QString &kind);
// The kinds crashForTest() knows.
QStringList crashTestKinds();

// For the platform parts (QcCrashHandler_posix.cpp, QcCrashHandler_win.cpp): the reason, empty
// when none, and the log handle.
const char *reason();
int logHandle();
// The platform parts of install() and prepareThread().
bool installPlatform(const Setup &setup);
void preparePlatformThread();
// The platform part of crashForTest(): no dialog that would wait for a person.
void preparePlatformCrashTest();
// A fatal message (qFatal, Q_ASSERT) is about to end the process: on Windows Qt ends it with a fast
// fail that no handler sees, so the report and dump are written now; elsewhere the abort() that
// follows is reported.
void reportFatalMessage();

} // namespace QcCrashHandler
