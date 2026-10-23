#include "common/diagnostics/QcCrashHandler.h"

#include "common/core/QcLogging.h"

#include <QThread>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <memory>

using namespace Qt::StringLiterals;

namespace {

std::array<char, 1024> g_reason{};
std::atomic<int> g_logHandle{-1};
std::atomic<bool> g_installed{false};

[[noreturn]] void onTerminate()
{
    QcCrashHandler::setReason("std::terminate called");
    std::abort();
}

// Recurses until the stack is used up; the volatile buffer keeps every frame and the addition after
// the call keeps it from becoming a loop.
int overflowStack(std::uint64_t depth)
{
    std::array<volatile char, 4096> buffer{};
    buffer[0] = static_cast<char>(depth);
    return depth == 0 ? 0 : overflowStack(depth + 1) + buffer[0];
}

void crashHere(const QString &kind)
{
    if (kind == u"segv"_s) {
        int *volatile nowhere = nullptr;
        *nowhere = 42;
    } else if (kind == u"abort"_s) {
        std::abort();
    } else if (kind == u"fatal"_s) {
        qFatal("crash test");
    } else if (kind == u"terminate"_s) {
        std::terminate();
    } else if (kind == u"trap"_s) {
#if defined(_MSC_VER)
        __debugbreak();
#else
        __builtin_trap();
#endif
    } else if (kind == u"overflow"_s) {
        [[maybe_unused]] const int never = overflowStack(1);
    }
}

// Crashes this way on a thread of its own that was prepared like the program's threads; does not
// return.
void crashOnThread(const QString &kind)
{
    std::unique_ptr<QThread> thread(QThread::create([kind] {
        QcCrashHandler::prepareThread();
        qCWarning(lcDiagnostics).noquote() << u"Crashing on purpose on this thread (%1)"_s.arg(kind);
        crashHere(kind);
    }));
    thread->setObjectName(u"crash test"_s);
    thread->start();
    thread->wait();
}

} // namespace

namespace QcCrashHandler {

bool available()
{
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    return false;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(undefined_behavior_sanitizer)
    return false;
#else
    return true;
#endif
#else
    return true;
#endif
}

bool install(const Setup &setup)
{
    if (!available()) {
        qCInfo(lcDiagnostics, "Crash reports are off in a sanitizer build");
        return false;
    }
    std::set_terminate(onTerminate);
    if (!installPlatform(setup)) {
        qCWarning(lcDiagnostics).noquote() << u"Cannot install the crash handler for %1"_s.arg(setup.reportPath);
        return false;
    }
    g_installed.store(true);
    return true;
}

void prepareThread()
{
    if (!g_installed.load()) {
        return;
    }
    // Global on macOS and Linux; MSVC keeps the terminate handler per thread.
    std::set_terminate(onTerminate);
    preparePlatformThread();
}

void setReason(const QByteArray &reason)
{
    // Racing writers may mix their texts; the report still names a reason.
    const qsizetype length = std::min<qsizetype>(reason.size(), static_cast<qsizetype>(g_reason.size()) - 1);
    std::copy_n(reason.constData(), length, g_reason.begin());
    g_reason[static_cast<std::size_t>(length)] = '\0';
}

void setLogHandle(int handle)
{
    g_logHandle.store(handle);
}

const char *reason()
{
    return g_reason.data();
}

int logHandle()
{
    return g_logHandle.load();
}

bool crashForTest(const QString &kind)
{
    if (!crashTestKinds().contains(kind)) {
        return false;
    }
    preparePlatformCrashTest();
    if (const QString onThread = u"thread-"_s; kind.startsWith(onThread)) {
        crashOnThread(kind.sliced(onThread.size()));
    } else {
        crashHere(kind);
    }
    return false;
}

QStringList crashTestKinds()
{
    return {u"segv"_s, u"abort"_s, u"fatal"_s, u"terminate"_s, u"trap"_s, u"thread-segv"_s, u"thread-overflow"_s};
}

} // namespace QcCrashHandler
