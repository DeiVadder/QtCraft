#include "common/protocol/QcConnection.h"

#include "common/protocol/QcTransport.h"

#include <QLoggingCategory>

namespace {
Q_LOGGING_CATEGORY(lcConnection, "qtcraft.connection")
}

QcConnection::QcConnection(std::unique_ptr<QcTransport> transport, QcProtocol::Peer remote, Handler handler,
                           QObject *parent)
    : QObject(parent)
    , m_transport(std::move(transport))
    , m_remote(remote)
    , m_handler(std::move(handler))
{
    connect(m_transport.get(), &QcTransport::frameReceived, this, &QcConnection::onFrame);
    connect(m_transport.get(), &QcTransport::closed, this, &QcConnection::closed);
}

QcConnection::~QcConnection() = default;

void QcConnection::send(const QcMessage &message)
{
    if (m_transport->isOpen()) {
        m_transport->sendFrame(QcProtocol::encode(message));
    }
}

void QcConnection::close()
{
    m_transport->close();
}

bool QcConnection::isOpen() const
{
    return m_transport->isOpen();
}

QString QcConnection::lastError() const
{
    return m_lastError.isEmpty() ? m_transport->closeReason() : m_lastError;
}

qint64 QcConnection::pendingBytes() const
{
    return m_transport->pendingBytes();
}

void QcConnection::onFrame(const QByteArray &frame)
{
    QcResult<QcMessage> message = QcProtocol::decode(frame, m_remote);
    if (!message) {
        m_lastError = message.error();
        qCWarning(lcConnection) << "Closing connection after protocol error:" << m_lastError;
        m_transport->close();
        return;
    }
    m_handler(std::move(*message));
}
