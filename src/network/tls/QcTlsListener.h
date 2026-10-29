// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "network/tls/QcTlsCredentials.h"

#include <QTcpServer>

#if QC_TLS_ENABLED
#include <QSslConfiguration>
#endif

// A TCP listener for TLS (decision E-872): every accepted connection is a QSslSocket set up as the
// TLS server with the credentials (TLS 1.3 only, no client certificate). The handshake begins
// only with startEncryption(), so the connection limits see a connection before it costs a
// handshake. nextPendingConnection() hands out these sockets. Without TLS (QcTlsBackend::enabled)
// it is a plain QTcpServer.
class QcTlsListener : public QTcpServer
{
    Q_OBJECT

public:
    explicit QcTlsListener(const QcTlsCredentials &credentials, QObject *parent = nullptr);

    // Starts the server side of the handshake on a socket of nextPendingConnection(). Data written
    // before it ends waits and goes out encrypted.
    static void startEncryption(QTcpSocket *socket);

protected:
    void incomingConnection(qintptr descriptor) override;

private:
#if QC_TLS_ENABLED
    QSslConfiguration m_configuration;
#endif
};
