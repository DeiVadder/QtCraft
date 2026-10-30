// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "network/QcServerAddress.h"
#include "network/QcTcpTransport.h"

#include <QObject>

#include <memory>

class QTcpSocket;
class QTimer;

/*! \brief Opens a TLS connection to a server (name lookup, IPv4 or IPv6; decisions E-251, E-872)
    and hands it over as a transport once the handshake is done.

    The server's certificate is its own, signed by nobody: it is not checked against authorities,
    the caller compares peerFingerprint() with what it pinned (QcKnownServers) before it sends
    anything. Exactly one of connected() and failed() follows connectTo(). */
class QcTcpConnector : public QObject
{
    Q_OBJECT

public:
    /*! A connector without a connection, child of \a parent. */
    explicit QcTcpConnector(QObject *parent = nullptr);
    ~QcTcpConnector() override;
    Q_DISABLE_COPY_MOVE(QcTcpConnector)

    /*! Starts connecting to \a address, ending an attempt still running; fails after \a timeoutMs
        without a finished handshake. The transport gets \a limits. */
    void connectTo(const QcServerAddress &address, int timeoutMs = 10'000, const QcTcpTransport::Limits &limits = {});
    /*! Ends the attempt running, without a signal. */
    void abort();
    /*! Returns the open transport after connected(); null before and after taking it. */
    std::unique_ptr<QcTcpTransport> takeTransport();
    /*! Returns SHA-256 of the server's certificate after connected() (QcTlsFingerprint); empty in a
        build without TLS (QcTlsBackend::enabled). */
    QByteArray peerFingerprint() const { return m_fingerprint; }

signals:
    /*! The handshake is done; takeTransport() hands the transport over. */
    void connected();
    /*! The attempt failed for \a reason, a translated text for the player. */
    void failed(const QString &reason);

private:
    void finish(const QString &error);
    // The reason for the socket's error, for a player.
    QString errorText() const;
    // SHA-256 of the certificate the server showed; empty without TLS.
    [[nodiscard]] static QByteArray fingerprintOf(QTcpSocket *socket);

    // A QSslSocket with TLS, a plain QTcpSocket without (QcTlsBackend::enabled).
    QTcpSocket *m_socket = nullptr;
    QTimer *m_timeout = nullptr;
    QcTcpTransport::Limits m_limits;
    std::unique_ptr<QcTcpTransport> m_transport;
    QByteArray m_fingerprint;
    // The TCP connection stands, the handshake runs.
    bool m_handshaking = false;
    // Counts connectTo() and abort() calls: a failure reported later belongs to the attempt it was
    // made for and is dropped once that attempt is over.
    quint64 m_attempt = 0;
};
