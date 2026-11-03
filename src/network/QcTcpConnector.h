// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "network/QcJoinFailure.h"
#include "network/QcServerAddress.h"
#include "network/QcTcpTransport.h"

#include <QDeadlineTimer>
#include <QHostAddress>
#include <QList>
#include <QObject>

#include <memory>

class QHostInfo;
class QTcpSocket;
class QTimer;

/*! \brief Opens a TLS connection to a server (name lookup, IPv4 or IPv6; decisions E-251, E-872,
    E-964) and hands it over as a transport once the handshake is done.

    It looks the name up itself and tries its addresses in the order the system gives them, each
    with an equal share of the time left, so an address that never answers (IPv6 without IPv6)
    does not use up the time of the next one. A failure names the step, the address and every IP
    address tried (QcJoinFailure).

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
    /*! The attempt failed as \a failure says. */
    void failed(const QcJoinFailure &failure);

private:
    void onLookedUp(const QHostInfo &info);
    // Opens a socket to the next address of m_pending, with its share of the time left.
    void connectNext();
    void onSocketError();
    // The time of the whole attempt (or of the address, `wholeAttempt` false) is up.
    void onTimeout(bool wholeAttempt);
    void onEstablished();
    // Notes how the connection to the current address ended, for the log.
    void noteStep(const QString &what);
    // The step the attempt is in.
    QcJoinFailure::Step step() const;
    void fail(QcJoinFailure::Kind kind, const QString &detail);
    // Closes the socket of the current address (without a signal).
    void dropSocket();
    // The address being tried.
    const QHostAddress &current() const { return m_current; }
    // The id of the name lookup running; -1 when none runs.
    int lookupId() const { return m_lookupId; }
    void setLookupId(int id) { m_lookupId = id; }
    // SHA-256 of the certificate the server showed; empty without TLS.
    [[nodiscard]] static QByteArray fingerprintOf(QTcpSocket *socket);

    // A QSslSocket with TLS, a plain QTcpSocket without (QcTlsBackend::enabled).
    QTcpSocket *m_socket = nullptr;
    // The whole attempt, and the share of one address when more follow it.
    QTimer *m_timeout = nullptr;
    QTimer *m_addressTimeout = nullptr;
    QDeadlineTimer m_deadline;
    // What a failure reports: address, addresses tried, steps.
    // NOLINT(qc-member-access): the record of one attempt, reset whole by connectTo() and grown in place.
    QcJoinFailure m_failure;
    // The addresses not tried yet.
    // NOLINT(qc-member-access): a queue of one attempt, set by connectTo()/onLookedUp(), taken by connectNext().
    QList<QHostAddress> m_pending;
    // The address being tried (current()); set only by connectNext().
    QHostAddress m_current;
    // The id of the name lookup running; -1 when none runs (lookupId(), setLookupId()).
    int m_lookupId = -1;
    QcTcpTransport::Limits m_limits;
    std::unique_ptr<QcTcpTransport> m_transport;
    QByteArray m_fingerprint;
    // The TCP connection stands, the handshake runs.
    bool m_handshaking = false;
    // Counts connectTo() and abort() calls: a failure reported later belongs to the attempt it was
    // made for and is dropped once that attempt is over.
    quint64 m_attempt = 0;
};
