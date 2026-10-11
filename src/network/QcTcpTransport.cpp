#include "QcTcpTransport.h"

#include "QcSocketErrors.h"

#include <QCoreApplication>
#include <QHostAddress>
#include <QTcpSocket>
#include <QThread>
#include <QTimer>

#include <algorithm>

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
        if (error != QAbstractSocket::RemoteHostClosedError && m_open) {
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
    lingerAndDelete(m_socket, m_limits.lingerMs);
}

void QcTcpTransport::lingerAndDelete(QTcpSocket *socket, int lingerMs)
{
    // Frames sent just before (the reason of a Disconnect) may still wait in the socket's buffer.
    // They get lingerMs to leave without anybody waiting for them: the socket deletes itself once
    // it is disconnected, or is cut off after that time.
    // Whatever the system takes now goes out even if the program ends right after (a stopping
    // server); flush() never blocks.
    socket->setParent(nullptr);
    socket->flush();
    if (socket->state() == QAbstractSocket::ConnectedState) {
        socket->disconnectFromHost();
    }
    if (socket->state() == QAbstractSocket::UnconnectedState) {
        delete socket;
        return;
    }

    QObject::connect(socket, &QAbstractSocket::disconnected, socket, &QObject::deleteLater);
    QTimer::singleShot(lingerMs, socket, [socket] {
        socket->abort();
        socket->deleteLater();
    });
    // Nothing is left behind when the socket's thread ends first (the server thread, the program).
    if (QCoreApplication *application = QCoreApplication::instance(); application && socket->thread() == application->thread()) {
        socket->setParent(application);
    } else {
        QObject::connect(socket->thread(), &QThread::finished, socket, &QObject::deleteLater, Qt::DirectConnection);
    }
}

void QcTcpTransport::sendFrame(const QByteArray &frame)
{
    if (!m_open) {
        return;
    }

    m_socket->write(QcFrameReader::header(frame.size()));
    m_socket->write(frame);
    m_lastSent.restart();
    if (m_socket->bytesToWrite() > m_limits.maxPendingBytes) {
        fail(QCoreApplication::translate("QcNetwork", "The connection is too slow"));
    }
}

void QcTcpTransport::close()
{
    if (!m_open) {
        return;
    }

    // Queued frames still go out; the socket closes once they are written.
    m_open = false;
    m_timer->stop();
    m_socket->disconnectFromHost();
    announceClosed();
}

qint64 QcTcpTransport::pendingBytes() const
{
    return m_socket->bytesToWrite();
}

void QcTcpTransport::onReadyRead()
{
    if (!m_open) {
        m_socket->readAll();
        return;
    }

    m_lastReceived.restart();
    if (!m_reader.append(m_socket->readAll())) {
        fail(QCoreApplication::translate("QcNetwork", "The other side sent a frame that is too large"));
        return;
    }

    while (m_open) {
        std::optional<QByteArray> frame = m_reader.next();
        if (!frame) {
            break;
        }
        // Empty frames only keep the connection alive.
        if (!frame->isEmpty()) {
            emit frameReceived(*frame);
        }
    }

    // A frame too large behind others of the same read is only seen while handing them out.
    if (m_reader.failed()) {
        fail(QCoreApplication::translate("QcNetwork", "The other side sent a frame that is too large"));
    }
}

void QcTcpTransport::onDisconnected()
{
    // The last frames of the peer (a Disconnect with its reason) count before the end.
    if (m_open && m_socket->bytesAvailable() > 0) {
        onReadyRead();
    }
    if (m_open && m_closeReason.isEmpty()) {
        m_closeReason = QCoreApplication::translate("QcNetwork", "The other side closed the connection");
    }
    m_open = false;
    m_timer->stop();
    announceClosed();
}

void QcTcpTransport::onTimer()
{
    if (!m_open) {
        return;
    }

    if (m_lastReceived.hasExpired(m_limits.idleTimeoutMs)) {
        fail(QCoreApplication::translate("QcNetwork", "The connection timed out"));
        return;
    }
    if (m_lastSent.hasExpired(m_limits.keepAliveMs)) {
        sendFrame({});
    }
}

void QcTcpTransport::fail(const QString &reason)
{
    if (!m_open) {
        return;
    }

    m_closeReason = reason;
    m_open = false;
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
