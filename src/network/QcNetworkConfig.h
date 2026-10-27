#pragma once

#include "network/QcTcpTransport.h"
#include "network/tls/QcTlsCredentials.h"

#include <QHostAddress>

// Bounds of new connections per network address (decision E-254), kept by the server's
// QcConnectionLimiter: a token bucket per address against connection floods, and a cap of open
// connections per address and in all.
struct QcConnectionLimits
{
    // New connections per second and address in the long run, and the burst allowed at once.
    double perSecond = 1.0;
    double burst = 8.0;
    // Open connections (logging in or playing) per address and in all.
    int perAddress = 8;
    int total = 64;
};

// Settings of the listener of a network game (decisions E-251, E-254, E-872).
struct QcNetworkConfig
{
    // The certificate and key of the TLS handshake (QcTlsCredentials::loadOrCreate in the data
    // folder); a listener without them does not start.
    QcTlsCredentials tls;
    // Any: IPv4 and IPv6 on all interfaces (LAN and internet); LocalHost or LocalHostIPv6 for this
    // computer only.
    QHostAddress bindAddress = QHostAddress::Any;
    // 0 picks a free port (tests).
    quint16 port = 25600;
    QcConnectionLimits connections;
    // Clients send only small messages; a frame beyond this ends their connection. A player's link
    // may be slow (home upload): the goodbye with a Disconnect's reason gets 2.5 s once the system
    // sent it (decision E-904).
    QcTcpTransport::Limits transport{.maxIncomingFrameBytes = qint64{256} * 1024, .lingerMs = 2'500};
};
