#include "server/network/QcNetworkServer.h"

#include "server/core/QcServer.h"

#include <QLoggingCategory>
#include <QTcpServer>
#include <QTcpSocket>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcNetwork, "qtcraft.network")

} // namespace

QcNetworkServer::QcNetworkServer(QcServer &server, QcNetworkConfig config, QObject *parent)
    : QObject(parent)
    , m_server(server)
    , m_config(std::move(config))
    , m_listener(new QTcpServer(this))
    , m_limiter(m_config.connections)
{
    m_clock.start();
    connect(m_listener, &QTcpServer::pendingConnectionAvailable, this, &QcNetworkServer::acceptPending);
}

QcNetworkServer::~QcNetworkServer() = default;

QcResult<quint16> QcNetworkServer::listen()
{
    if (!m_listener->listen(m_config.bindAddress, m_config.port)) {
        return qcError(u"Cannot listen on port %1: %2"_s.arg(m_config.port).arg(m_listener->errorString()));
    }

    qCInfo(lcNetwork).noquote() << u"Listening on %1 port %2"_s.arg(m_config.bindAddress.toString()).arg(m_listener->serverPort());
    return m_listener->serverPort();
}

quint16 QcNetworkServer::port() const
{
    return m_listener->serverPort();
}

void QcNetworkServer::acceptPending()
{
    while (QTcpSocket *socket = m_listener->nextPendingConnection()) {
        // Per address without the port (IPv6 per /64 network): one computer opening many
        // connections is one source.
        const QString address = QcConnectionLimiter::sourceOf(socket->peerAddress());
        if (!m_limiter.admit(address, m_clock.elapsed())) {
            ++m_refused;
            qCInfo(lcNetwork).noquote() << u"Connection from %1 refused: too many connections"_s.arg(address);
            socket->abort();
            socket->deleteLater();
            continue;
        }

        auto transport = std::make_unique<QcTcpTransport>(socket, m_config.transport);
        connect(transport.get(), &QObject::destroyed, this, [this, address] { m_limiter.closed(address); });
        qCInfo(lcNetwork).noquote() << u"Connection from %1"_s.arg(transport->peerAddress());
        m_server.addClient(std::move(transport), QcServer::ClientOrigin::Remote);
    }
}
