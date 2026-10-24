#pragma once

#include "network/QcNetworkConfig.h"
#include "network/tls/QcTlsCredentials.h"

// The TLS certificate of the servers in a test process (decision E-872): made once on first use
// (a few hundred milliseconds) and shared by every listener of the process.
namespace QcTestTls {

inline const QcTlsCredentials &credentials()
{
    static const QcTlsCredentials made = QcTlsCredentials::generate().value_or(QcTlsCredentials());
    return made;
}

// A listener configuration on 127.0.0.1 with a free port and the test certificate.
inline QcNetworkConfig localConfig()
{
    QcNetworkConfig network;
    network.bindAddress = QHostAddress::LocalHost;
    network.port = 0;
    network.tls = credentials();
    return network;
}

} // namespace QcTestTls
