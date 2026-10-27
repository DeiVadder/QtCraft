#include "network/QcTcpTransport.h"

#include "network/QcSocketErrors.h"
#include "network/QcTcpGoodbye.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>
#include <memory>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

QString addressText(const QTcpSocket &socket)
{
    const QHostAddress address = socket.peerAddress();
    const QString host = address.protocol() == QAbstractSocket::IPv6Protocol ? u"[%1]"_s.arg(address.toString()) : address.toString();
    return u"%1:%2"_s.arg(host).arg(socket.peerPort());
}

} // namespace

QcTcpTransport::QcTcpTransport(QTcpSocket *socket, const Limits &limits, QObject *parent)
    : QcTransport(parent)
    , m_socket(socket)
    , m_timer(new QTimer(this))
    , m_limits(limits)
    , m_reader(limits.maxIncomingFrameBytes)
    , m_peerAddress(addressText(*socket))
{
    m_socket->setParent(this);
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    connect(m_socket, &QTcpSocket::readyRead, this, &QcTcpTransport::onReadyRead);
    connect(m_socket, &QTcpSocket::disconnected, this, &QcTcpTransport::onDisconnected);
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError error) {
        // The peer closing its end is the normal end of a connection, not an error.
        if (error != QAbstractSocket::RemoteHostClosedError && isOpen()) {
            fail(QcSocketErrors::describe(m_socket->error(), m_socket->errorString()));
        }
    });

    m_lastReceived.start();
    m_lastSent.start();
    m_timer->setInterval(std::clamp(std::min(m_limits.keepAliveMs, m_limits.idleTimeoutMs) / 4, 10, 1000));
    connect(m_timer, &QTimer::timeout, this, &QcTcpTransport::onTimer);
    m_timer->start();

    if (m_socket->state() != QAbstractSocket::ConnectedState) {
        fail(QCoreApplication::translate("QcNetwork", "Not connected"));
    } else if (m_socket->bytesAvailable() > 0) {
        QMetaObject::invokeMethod(this, &QcTcpTransport::onReadyRead, Qt::QueuedConnection);
    }
}

QcTcpTransport::~QcTcpTransport()
{
    m_socket->disconnect(this);
    // The socket is a child so it follows the transport into another thread; here it leaves it.
    m_socket->setParent(nullptr);
    // Frames sent just before (the reason of a Disconnect) may still wait in the socket's buffer;
    // the goodbye sends them and the end in the background, nothing waits for it (decision E-904).
    QcTcpGoodbye::release(std::unique_ptr<QTcpSocket>(std::exchange(m_socket, nullptr)), limits().lingerMs, peerLeft());
}

void QcTcpTransport::finishLingering()
{
    QcTcpGoodbye::finishLingering();
}

bool QcTcpTransport::keepsItsThread() const
{
    return m_socket && m_socket->inherits("QSslSocket");
}

void QcTcpTransport::sendFrame(const QByteArray &frame)
{
    if (!isOpen()) {
        return;
    }

    m_socket->write(QcFrameReader::header(frame.size()));
    m_socket->write(frame);
    m_lastSent.restart();
    if (pendingBytes() > limits().maxPendingBytes) {
        fail(QCoreApplication::translate("QcNetwork", "The connection is too slow"));
    }
}

void QcTcpTransport::close()
{
    if (!isOpen()) {
        return;
    }

    // Queued frames still go out, then the end (decision E-904).
    setOpen(false);
    m_timer->stop();
    QcTcpGoodbye::begin(*m_socket, limits().lingerMs, peerLeft());
    announceClosed();
}

qint64 QcTcpTransport::pendingBytes() const
{
    // Over TLS the bytes not encrypted yet and those encrypted but not sent.
    if (const auto *secure = qobject_cast<const QSslSocket *>(m_socket)) {
        return secure->bytesToWrite() + secure->encryptedBytesToWrite();
    }
    return m_socket->bytesToWrite();
}

void QcTcpTransport::onReadyRead()
{
    if (!isOpen()) {
        m_socket->readAll();
        return;
    }

    m_lastReceived.restart();
    if (!reader().append(m_socket->readAll())) {
        fail(QCoreApplication::translate("QcNetwork", "The other side sent a frame that is too large"));
        return;
    }

    while (isOpen()) {
        std::optional<QByteArray> frame = reader().next();
        if (!frame) {
            break;
        }
        // Empty frames only keep the connection alive.
        if (!frame->isEmpty()) {
            emit frameReceived(*frame);
        }
    }

    // A frame too large behind others of the same read is only seen while handing them out.
    if (reader().failed()) {
        fail(QCoreApplication::translate("QcNetwork", "The other side sent a frame that is too large"));
    }
}

void QcTcpTransport::onDisconnected()
{
    // The last frames of the peer (a Disconnect with its reason) count before the end.
    if (isOpen() && m_socket->bytesAvailable() > 0) {
        onReadyRead();
    }
    if (isOpen() && closeReason().isEmpty()) {
        setCloseReason(QCoreApplication::translate("QcNetwork", "The other side closed the connection"));
    }
    setOpen(false);
    m_timer->stop();
    announceClosed();
}

void QcTcpTransport::onTimer()
{
    if (!isOpen()) {
        return;
    }

    if (m_lastReceived.hasExpired(limits().idleTimeoutMs)) {
        fail(QCoreApplication::translate("QcNetwork", "The connection timed out"));
        return;
    }
    if (m_lastSent.hasExpired(limits().keepAliveMs)) {
        sendFrame({});
    }
}

void QcTcpTransport::fail(const QString &reason)
{
    if (!isOpen()) {
        return;
    }

    setCloseReason(reason);
    setOpen(false);
    m_timer->stop();
    m_socket->abort();
    announceClosed();
}

void QcTcpTransport::announceClosed()
{
    if (m_closedAnnounced) {
        return;
    }

    m_closedAnnounced = true;
    // Like the in-process transport: closed() never comes from inside send() or close().
    QMetaObject::invokeMethod(this, &QcTcpTransport::closed, Qt::QueuedConnection);
}
