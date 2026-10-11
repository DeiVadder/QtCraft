#include "QcSignalWatcher.h"

#include <QSocketNotifier>

#include <atomic>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace Qt::StringLiterals;

namespace {

std::atomic<QcSignalWatcher *> watcher{nullptr};

#ifdef Q_OS_WIN

BOOL WINAPI consoleHandler(DWORD event)
{
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT && event != CTRL_CLOSE_EVENT && event != CTRL_SHUTDOWN_EVENT) {
        return FALSE;
    }
    if (QcSignalWatcher *target = watcher.load()) {
        QMetaObject::invokeMethod(target, [target] { emit target->stopRequested(u"console event"_s); }, Qt::QueuedConnection);
        return TRUE;
    }
    return FALSE;
}

#else

// The handler only writes the signal number into a socket; the notifier reads it in the event loop.
int signalSockets[2] = {-1, -1};

void handleSignal(int number)
{
    const char byte = char(number);
    [[maybe_unused]] const ssize_t written = ::write(signalSockets[0], &byte, 1);
    // Back to the default: a second request ends the process at once.
    std::signal(number, SIG_DFL);
}

#endif

} // namespace

QcSignalWatcher::QcSignalWatcher(QObject *parent)
    : QObject(parent)
{
    QcSignalWatcher *expected = nullptr;
    if (!watcher.compare_exchange_strong(expected, this)) {
        return;
    }

#ifdef Q_OS_WIN
    m_watching = SetConsoleCtrlHandler(consoleHandler, TRUE) != 0;
#else
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, signalSockets) != 0) {
        watcher.store(nullptr);
        return;
    }
    m_notifier = new QSocketNotifier(qintptr(signalSockets[1]), QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &QcSignalWatcher::onRequest);

    struct sigaction action{};
    action.sa_handler = handleSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    m_watching = ::sigaction(SIGTERM, &action, nullptr) == 0 && ::sigaction(SIGINT, &action, nullptr) == 0;
#endif
}

QcSignalWatcher::~QcSignalWatcher()
{
    if (watcher.load() != this) {
        return;
    }

#ifdef Q_OS_WIN
    SetConsoleCtrlHandler(consoleHandler, FALSE);
#else
    std::signal(SIGTERM, SIG_DFL);
    std::signal(SIGINT, SIG_DFL);
    delete m_notifier;
    ::close(signalSockets[0]);
    ::close(signalSockets[1]);
    signalSockets[0] = signalSockets[1] = -1;
#endif
    watcher.store(nullptr);
}

void QcSignalWatcher::onRequest()
{
#ifndef Q_OS_WIN
    char byte = 0;
    if (::read(signalSockets[1], &byte, 1) != 1) {
        return;
    }
    emit stopRequested(int(byte) == SIGTERM ? u"SIGTERM"_s : u"SIGINT"_s);
#endif
}
