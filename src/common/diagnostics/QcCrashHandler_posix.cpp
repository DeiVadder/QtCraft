// The crash handler of macOS and Linux (decision E-834): a signal handler on its own stack that
// writes the report with async-signal-safe calls only (open, write, close, nanosleep,
// backtrace_symbols_fd) from buffers filled at install time, then raises the signal again with the
// default action. The main thread's alternate stack is static, other threads get theirs from
// prepareThread().

#include "common/diagnostics/QcCrashHandler.h"

#include <QFile>

#include <algorithm>
#include <array>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <ctime>
#include <memory>
#include <span>

#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>
#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define QC_HAS_BACKTRACE 1
#endif
#if defined(Q_OS_LINUX)
#include <sys/syscall.h>
#endif

namespace {

// SIGTRAP: __builtin_trap and the checks of the C++ library on arm64; SIGSYS: a bad system call.
constexpr std::array fatalSignals{SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP, SIGSYS};
constexpr int maxFrames = 128;
constexpr std::size_t signalStackBytes = std::size_t(128) * 1024;
// How long a second crashing thread waits for the report of the first.
constexpr int waitForReportMs = 5000;

std::array<char, 2048> g_reportPath{};
std::array<char, 4096> g_header{};
// The main thread's stack for the handler, so a stack overflow can still be reported.
alignas(16) std::array<char, signalStackBytes> g_signalStack{};
std::atomic_flag g_handling;
std::atomic<bool> g_reported{false};
// The thread that writes the report (0: none).
std::atomic<std::uint64_t> g_reportingThread{0};

// The alternate signal stack of a thread other than the main thread (prepareThread()): switched off
// before its memory goes when the thread ends.
struct ThreadSignalStack
{
    std::unique_ptr<char[]> memory;

    ThreadSignalStack() = default;
    ~ThreadSignalStack()
    {
        if (memory) {
            stack_t off{};
            off.ss_flags = SS_DISABLE;
            sigaltstack(&off, nullptr);
        }
    }
    Q_DISABLE_COPY_MOVE(ThreadSignalStack)
};

thread_local ThreadSignalStack t_signalStack;

std::size_t textLength(const char *text)
{
    std::size_t length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

void writeText(int fd, const char *text)
{
    // Nothing to do about a failed write in a crashing process.
    [[maybe_unused]] const ssize_t written = write(fd, text, textLength(text));
}

// Writes `value` in base 10 or 16 without allocating.
void writeNumber(int fd, std::uintmax_t value, unsigned base)
{
    std::array<char, 32> digits{};
    std::size_t at = digits.size() - 1;
    do {
        digits[--at] = "0123456789abcdef"[value % base];
        value /= base;
    } while (value != 0 && at > 0);
    writeText(fd, digits.data() + at);
}

const char *signalName(int signal)
{
    switch (signal) {
    case SIGSEGV:
        return "SIGSEGV (invalid memory access)";
    case SIGBUS:
        return "SIGBUS (bus error)";
    case SIGILL:
        return "SIGILL (illegal instruction)";
    case SIGFPE:
        return "SIGFPE (arithmetic error)";
    case SIGABRT:
        return "SIGABRT (abort)";
    case SIGTRAP:
        return "SIGTRAP (trap instruction)";
    case SIGSYS:
        return "SIGSYS (bad system call)";
    default:
        return "unknown signal";
    }
}

void writeReport(int fd, int signal, const siginfo_t *info)
{
    writeText(fd, g_header.data());
    writeText(fd, "signal: ");
    writeNumber(fd, std::uintmax_t(signal), 10);
    writeText(fd, " ");
    writeText(fd, signalName(signal));
    writeText(fd, "\naddress: 0x");
    writeNumber(fd, info ? reinterpret_cast<std::uintptr_t>(info->si_addr) : 0, 16);
    writeText(fd, "\nthread: ");
    writeNumber(fd, QcCrashHandler::threadId(), 10);
    if (const char *reason = QcCrashHandler::reason(); reason[0] != '\0') {
        writeText(fd, "\nreason: ");
        writeText(fd, reason);
    }
    writeText(fd, "\nbacktrace:\n");
#if defined(QC_HAS_BACKTRACE)
    std::array<void *, maxFrames> frames{};
    const int count = backtrace(frames.data(), maxFrames);
    backtrace_symbols_fd(frames.data(), count, fd);
#else
    writeText(fd, "(not available on this platform)\n");
#endif
}

void writeCrash(int signal, const siginfo_t *info)
{
    const int fd = open(g_reportPath.data(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd >= 0) {
        writeReport(fd, signal, info);
        close(fd);
    }
    if (const int log = QcCrashHandler::logHandle(); log >= 0) {
        writeText(log, "*** CRASH: ");
        writeText(log, signalName(signal));
        writeText(log, ", report: ");
        writeText(log, g_reportPath.data());
        writeText(log, "\n");
    }
}

// Another thread writes the report: give it time (bounded) before this thread's signal ends the
// process. The thread that writes and crashes again ends at once.
void waitForReport()
{
    if (g_reportingThread.load() == QcCrashHandler::threadId()) {
        return;
    }
    const timespec step{0, 10'000'000};
    for (int waited = 0; waited < waitForReportMs && !g_reported.load(); waited += 10) {
        nanosleep(&step, nullptr);
    }
}

void onSignal(int signal, siginfo_t *info, void * /*context*/)
{
    if (!g_handling.test_and_set()) {
        g_reportingThread.store(QcCrashHandler::threadId());
        writeCrash(signal, info);
        g_reported.store(true);
    } else {
        waitForReport();
    }
    // The default action again: SA_RESETHAND does not reset SIGILL and SIGTRAP on every system
    // (a trap instruction would call this handler for ever). The signal is blocked until the handler
    // returns, then it ends the process as it would have without the handler.
    struct sigaction standard{};
    standard.sa_handler = SIG_DFL;
    sigemptyset(&standard.sa_mask);
    sigaction(signal, &standard, nullptr);
    raise(signal);
}

bool fits(std::span<char> target, const QByteArray &text)
{
    return text.size() < qsizetype(target.size());
}

// Copies `text` with a terminating zero, cut to the size of `target`.
void copyInto(std::span<char> target, const QByteArray &text)
{
    const std::size_t length = std::min(std::size_t(text.size()), target.size() - 1);
    std::copy_n(text.constData(), length, target.begin());
    target[length] = '\0';
}

} // namespace

namespace QcCrashHandler {

bool installPlatform(const Setup &setup)
{
    // A cut path would write the report somewhere else; a cut header still leaves a report.
    const QByteArray path = QFile::encodeName(setup.reportPath);
    if (!fits(g_reportPath, path)) {
        return false;
    }
    copyInto(g_reportPath, path);
    copyInto(g_header, setup.header);
#if defined(QC_HAS_BACKTRACE)
    // The first call may load libraries: not in the signal handler.
    std::array<void *, 4> warmUp{};
    backtrace(warmUp.data(), int(warmUp.size()));
#endif

    stack_t stack{};
    stack.ss_sp = g_signalStack.data();
    stack.ss_size = g_signalStack.size();
    sigaltstack(&stack, nullptr);

    struct sigaction action{};
    action.sa_sigaction = onSignal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    bool ok = true;
    for (const int signal : fatalSignals) {
        // Replaces an earlier handler of the signal without keeping it (see QcCrashHandler.h).
        ok = sigaction(signal, &action, nullptr) == 0 && ok;
    }
    return ok;
}

void preparePlatformThread()
{
    stack_t current{};
    // Done before, or a stack of its own already (the main thread).
    if (t_signalStack.memory || (sigaltstack(nullptr, &current) == 0 && (current.ss_flags & SS_DISABLE) == 0)) {
        return;
    }
    t_signalStack.memory = std::make_unique_for_overwrite<char[]>(signalStackBytes);
    stack_t stack{};
    stack.ss_sp = t_signalStack.memory.get();
    stack.ss_size = signalStackBytes;
    sigaltstack(&stack, nullptr);
}

quint64 threadId()
{
#if defined(Q_OS_DARWIN)
    std::uint64_t id = 0;
    pthread_threadid_np(nullptr, &id);
    return id;
#elif defined(Q_OS_LINUX)
    return quint64(syscall(SYS_gettid));
#else
    return quint64(reinterpret_cast<std::uintptr_t>(pthread_self()));
#endif
}

void preparePlatformCrashTest()
{
    // Nothing to prepare: the default action of the signal ends the process without a dialog.
}

void reportFatalMessage()
{
    // abort() follows and its SIGABRT writes the report with the reason.
}

} // namespace QcCrashHandler
