#pragma once

#include "common/core/QcResult.h"
#include "network/QcTcpTransport.h"
#include "server/network/QcConnectionLimiter.h"

#include <QHostAddress>
#include <QObject>

class QcServer;
class QTcpServer;

// Settings of the listener of a network game (decisions E-251, E-254).
struct QcNetworkConfig
{
    // Any: IPv4 and IPv6 on all interfaces (LAN and internet); LocalHost or LocalHostIPv6 for this
    // computer only.
    QHostAddress bindAddress = QHostAddress::Any;
    // 0 picks a free port (tests).
    quint16 port = 25600;
    QcConnectionLimiter::Limits connections;
    // Clients send only small messages; a frame beyond this ends their connection.
    QcTcpTransport::Limits transport{.maxIncomingFrameBytes = qint64(256) * 1024};
};

// Accepts TCP connections for a QcServer in the server's thread (decision E-251): every accepted
// socket becomes a QcTcpTransport and a remote client of the server, which logs it in; no data is
// shared with another thread, so the game state needs no locks. New connections pass the
// QcConnectionLimiter first.
class QcNetworkServer : public QObject
{
    Q_OBJECT

public:
    QcNetworkServer(QcServer &server, QcNetworkConfig config, QObject *parent = nullptr);
    ~QcNetworkServer() override;

    // Starts listening; the port in use (the configured one, or a free one for port 0).
    [[nodiscard]] QcResult<quint16> listen();
    quint16 port() const;
    int openConnections() const { return m_limiter.open(); }
    // Connections refused by the limits since the start.
    quint64 refusedConnections() const { return m_refused; }

private:
    void acceptPending();

    QcServer &m_server;
    QcNetworkConfig m_config;
    QTcpServer *m_listener = nullptr;
    QcConnectionLimiter m_limiter;
    QElapsedTimer m_clock;
    quint64 m_refused = 0;
};
