#include "network/QcTcpGoodbye.h"

#include "common/core/QcLogging.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QPointer>
#include <QSslSocket>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cerrno>
#include <optional>
#include <utility>
#include <vector>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#else
#include <sys/ioctl.h>
#include <sys/socket.h>
#endif
#ifdef Q_OS_LINUX
#include <linux/sockios.h>
#endif

namespace {

// The sockets released in this thread when it is not the application's (finishLingering()).
thread_local std::vector<QPointer<QTcpSocket>> t_lingering;

// How often the goodbye looks whether the system sent everything.
constexpr int pollMs = 20;
// A goodbye ends at the latest after this many linger times (a peer that reads nothing).
constexpr int limitInLingerTimes = 4;

// Shuts the write side of the connection down: the peer reads the end after everything the system
// took before. Reading stays possible.
bool shutdownWrite(qintptr descriptor)
{
    if (descriptor < 0) {
        return false;
    }
#ifdef Q_OS_WIN
    return ::shutdown(static_cast<SOCKET>(descriptor), SD_SEND) == 0;
#else
    return ::shutdown(static_cast<int>(descriptor), SHUT_WR) == 0;
#endif
}

// Bytes the system has not sent or the peer not acknowledged yet; empty where the system does not
// tell (the goodbye then counts the linger time at once).
std::optional<qint64> unsentBytes(qintptr descriptor)
{
    if (descriptor < 0) {
        return std::nullopt;
    }
#if defined(Q_OS_WIN) && defined(SIO_TCP_INFO)
    DWORD version = 0;
    TCP_INFO_v0 info{};
    DWORD returned = 0;
    if (::WSAIoctl(static_cast<SOCKET>(descriptor), SIO_TCP_INFO, &version, sizeof(version), &info, sizeof(info), &returned, nullptr, nullptr) == 0) {
        return static_cast<qint64>(info.BytesInFlight);
    }
#elif defined(SO_NWRITE)
    int bytes = 0;
    socklen_t length = sizeof(bytes);
    if (::getsockopt(static_cast<int>(descriptor), SOL_SOCKET, SO_NWRITE, &bytes, &length) == 0) {
        return bytes;
    }
#elif defined(SIOCOUTQ)
    int bytes = 0;
    if (::ioctl(static_cast<int>(descriptor), SIOCOUTQ, &bytes) == 0) {
        return bytes;
    }
#endif
    // Once per program: which path the goodbye takes here (the Windows build in the VM).
    static const bool reported = [] {
        qCDebug(lcConnection) << "The system does not tell its send queue; the linger time counts from the end of Qt's buffers";
        return true;
    }();
    Q_UNUSED(reported)
    return std::nullopt;
}

// The system's error code of the latest socket call, for the log.
int lastSocketError()
{
#ifdef Q_OS_WIN
    return ::WSAGetLastError();
#else
    return errno;
#endif
}

// Whether the socket speaks TLS (a QSslSocket of a build without TLS stays plain).
bool isEncrypted(const QTcpSocket &socket)
{
    const auto *secure = qobject_cast<const QSslSocket *>(&socket);
    return secure && secure->mode() != QSslSocket::UnencryptedMode;
}

// Bytes Qt still holds for the socket; over TLS also those not encrypted yet (waiting for the
// handshake) and those encrypted but not handed to the system.
qint64 heldBytes(const QTcpSocket &socket)
{
    if (const auto *secure = qobject_cast<const QSslSocket *>(&socket); secure && isEncrypted(socket)) {
        return secure->bytesToWrite() + secure->encryptedBytesToWrite();
    }
    return socket.bytesToWrite();
}

} // namespace

QcTcpGoodbye::QcTcpGoodbye(QTcpSocket &socket, int lingerMs, bool peerLeft)
    : QObject(&socket)
    , m_socket(socket)
    , m_poll(new QTimer(this))
    , m_linger(new QTimer(this))
    , m_limit(new QTimer(this))
    , m_lingerMs(lingerMs)
    // Qt's TLS socket throws away encrypted data that arrives in the same read as the end, so over
    // TLS the peer closes first, on the Disconnect it read (decision E-904).
    , m_endEarly(peerLeft || !isEncrypted(socket))
{
    connect(&m_socket, &QIODevice::readyRead, this, &QcTcpGoodbye::discardReceived);
    connect(&m_socket, &QIODevice::bytesWritten, this, &QcTcpGoodbye::step);
    if (auto *secure = qobject_cast<QSslSocket *>(&m_socket)) {
        connect(secure, &QSslSocket::encryptedBytesWritten, this, &QcTcpGoodbye::step);
    }
    // Queued: never inside Qt's own disconnect sequence or the socket's destructor.
    connect(
        &m_socket, &QAbstractSocket::stateChanged, this,
        [this](QAbstractSocket::SocketState state) {
            if (state == QAbstractSocket::UnconnectedState) {
                finish();
            }
        },
        Qt::QueuedConnection);

    m_linger->setSingleShot(true);
    connect(m_linger, &QTimer::timeout, this, &QcTcpGoodbye::finish);
    m_limit->setSingleShot(true);
    connect(m_limit, &QTimer::timeout, this, &QcTcpGoodbye::finish);
    m_limit->start(limitInLingerTimes * lingerMs);
    connect(m_poll, &QTimer::timeout, this, &QcTcpGoodbye::step);
    m_poll->start(pollMs);

    discardReceived();
    // Hands whatever the system takes now over without blocking.
    m_socket.flush();
    step();
}

void QcTcpGoodbye::begin(QTcpSocket &socket, int lingerMs, bool peerLeft)
{
    Q_ASSERT(socket.thread() == QThread::currentThread());
    if (socket.findChild<QcTcpGoodbye *>(Qt::FindDirectChildrenOnly)) {
        return;
    }
    if (socket.state() != QAbstractSocket::ConnectedState) {
        socket.abort();
        return;
    }
    new QcTcpGoodbye(socket, lingerMs, peerLeft);
}

void QcTcpGoodbye::release(std::unique_ptr<QTcpSocket> owned, int lingerMs, bool peerLeft)
{
    begin(*owned, lingerMs, peerLeft);
    auto *goodbye = owned->findChild<QcTcpGoodbye *>(Qt::FindDirectChildrenOnly);
    if (!goodbye || goodbye->hasFinished()) {
        return;
    }

    // From here on the socket deletes itself.
    QTcpSocket *socket = owned.release();
    goodbye->m_released = true;
    // Nothing is left behind when the socket's thread ends first (the server thread, the program).
    if (QCoreApplication *application = QCoreApplication::instance(); application && socket->thread() == application->thread()) {
        socket->setParent(application);
    } else {
        connect(socket->thread(), &QThread::finished, goodbye, &QcTcpGoodbye::onThreadFinished, Qt::DirectConnection);
        std::erase_if(t_lingering, [](const QPointer<QTcpSocket> &lingering) { return lingering.isNull(); });
        t_lingering.emplace_back(socket);
    }
}

void QcTcpGoodbye::finishLingering(int maxWaitMs)
{
    const auto open = [] {
        return std::ranges::any_of(t_lingering, [](const QPointer<QTcpSocket> &socket) {
            return socket && socket->state() != QAbstractSocket::UnconnectedState;
        });
    };
    // All goodbyes run at the same time; the thread waits for the slowest, at most maxWaitMs.
    const QDeadlineTimer deadline(maxWaitMs);
    while (open() && !deadline.hasExpired()) {
        QEventLoop loop;
        QTimer::singleShot(10, &loop, &QEventLoop::quit);
        loop.exec();
    }
    for (const QPointer<QTcpSocket> &socket : std::as_const(t_lingering)) {
        if (auto *goodbye = socket ? socket->findChild<QcTcpGoodbye *>(Qt::FindDirectChildrenOnly) : nullptr) {
            goodbye->finish();
        }
    }
    t_lingering.clear();
}

void QcTcpGoodbye::step()
{
    if (hasFinished() || heldBytes(m_socket) > 0) {
        return;
    }

    if (m_endEarly && !m_endSent) {
        m_endSent = true;
        if (!shutdownWrite(m_socket.socketDescriptor())) {
            qCDebug(lcConnection) << "Ending the connection failed, closing it: system error" << lastSocketError();
            finish();
            return;
        }
    }
    // The linger time counts once the system sent everything: on a slow link the frames may need
    // longer than that to leave.
    if (m_poll->isActive() && unsentBytes(m_socket.socketDescriptor()).value_or(0) == 0) {
        m_poll->stop();
        m_linger->start(m_lingerMs);
    }
}

void QcTcpGoodbye::discardReceived()
{
    m_socket.readAll();
}

void QcTcpGoodbye::finish()
{
    if (m_finished) {
        return;
    }

    m_finished = true;
    m_poll->stop();
    m_linger->stop();
    m_limit->stop();
    // Read once more: data left unread when the socket closes makes the system reset the connection.
    if (m_socket.state() == QAbstractSocket::ConnectedState) {
        m_socket.waitForReadyRead(0);
    }
    discardReceived();
    m_socket.abort();
    if (m_released) {
        m_socket.deleteLater();
    }
}

void QcTcpGoodbye::onThreadFinished()
{
    // Nothing of the socket reaches the goodbye while the socket goes with the thread.
    m_socket.disconnect(this);
    m_socket.deleteLater();
}
