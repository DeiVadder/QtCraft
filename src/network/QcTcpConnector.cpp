// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "network/QcTcpConnector.h"

#include "common/core/QcLogging.h"
#include "network/tls/QcTlsBackend.h"
#include "network/tls/QcTlsCredentials.h"

#include <QHostInfo>
#include <QStringList>
#include <QTcpSocket>
#include <QTimer>

#if QC_TLS_ENABLED
#include <QSslConfiguration>
#include <QSslSocket>
#endif

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

// An address gets at least this much of the time left before the next one is tried.
constexpr qint64 minimumShareMs = 2'000;

} // namespace

QcTcpConnector::QcTcpConnector(QObject *parent)
    : QObject(parent)
    , m_timeout(new QTimer(this))
    , m_addressTimeout(new QTimer(this))
{
    m_timeout->setSingleShot(true);
    m_addressTimeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this] { onTimeout(true); });
    connect(m_addressTimeout, &QTimer::timeout, this, [this] { onTimeout(false); });
}

QcTcpConnector::~QcTcpConnector() = default;

void QcTcpConnector::connectTo(const QcServerAddress &address, int timeoutMs, const QcTcpTransport::Limits &limits)
{
    abort();
    m_limits = limits;
    m_fingerprint.clear();
    m_failure = {.kind = QcJoinFailure::Kind::Other, .address = address, .tried = {}, .reason = {}, .detail = {}, .steps = {}};
    if (const QcStatus tls = QcTlsBackend::ensure(); !tls) {
        // Reported like a socket error: after connectTo() returned, unless the attempt was
        // aborted or replaced by then.
        QTimer::singleShot(0, this, [this, attempt = m_attempt, reason = tls.error()] {
            if (attempt == m_attempt) {
                QcJoinFailure failure = m_failure;
                failure.reason = reason;
                emit failed(failure);
            }
        });
        return;
    }

    m_deadline = QDeadlineTimer(timeoutMs);
    m_timeout->start(timeoutMs);
    // An IP address needs no lookup (QHostInfo would look its name up in reverse).
    if (const QHostAddress ip(address.host); !ip.isNull()) {
        m_pending = {ip};
        connectNext();
        return;
    }
    setLookupId(QHostInfo::lookupHost(address.host, this, [this, attempt = m_attempt](const QHostInfo &info) {
        if (attempt == m_attempt) {
            onLookedUp(info);
        }
    }));
}

void QcTcpConnector::onLookedUp(const QHostInfo &info)
{
    setLookupId(-1);
    if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
        fail(QcJoinFailure::Kind::LookupFailed, info.errorString());
        return;
    }

    // In the order the system gives them: it puts IPv4 first on a computer without IPv6
    // (RFC 6724), and Qt's own connectToHost() keeps that order too.
    m_pending = info.addresses();
    QStringList ips;
    for (const QHostAddress &ip : std::as_const(m_pending)) {
        ips.append(ip.toString());
    }
    qCInfo(lcConnection).noquote() << u"%1 is %2"_s.arg(m_failure.address.host, ips.join(u", "_s));
    connectNext();
}

void QcTcpConnector::connectNext()
{
    m_current = m_pending.takeFirst();
    m_failure.tried.append(current());
    m_handshaking = false;
    // The last address has the whole time left (m_timeout).
    m_addressTimeout->stop();
    if (!m_pending.isEmpty()) {
        const qint64 share = m_deadline.remainingTime() / (m_pending.size() + 1);
        m_addressTimeout->start(static_cast<int>(std::max(share, minimumShareMs)));
    }

    const QString ip = current().toString();
#if QC_TLS_ENABLED
    auto *socket = new QSslSocket(this);
    QSslConfiguration configuration = QSslConfiguration::defaultConfiguration();
    // TLS 1.3 only: both ends are QtCraft with the same OpenSSL, so no older protocol, no
    // renegotiation and none of the cipher suites of TLS 1.2 are needed.
    configuration.setProtocol(QSsl::TlsV1_3OrLater);
    // The certificate is asked for but not judged by authorities: the caller checks its pin.
    configuration.setPeerVerifyMode(QSslSocket::QueryPeer);
    socket->setSslConfiguration(configuration);
    connect(socket, &QSslSocket::connected, this, [this] {
        m_addressTimeout->stop();
        m_handshaking = true;
    });
    connect(socket, &QSslSocket::sslErrors, this, [socket] { socket->ignoreSslErrors(); });
    connect(socket, &QSslSocket::encrypted, this, &QcTcpConnector::onEstablished);
    m_socket = socket;
    connect(m_socket, &QTcpSocket::errorOccurred, this, &QcTcpConnector::onSocketError);
    // The name the player typed goes to the server as the peer name (SNI).
    socket->connectToHostEncrypted(ip, m_failure.address.port, m_failure.address.host);
#else
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, &QcTcpConnector::onEstablished);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &QcTcpConnector::onSocketError);
    m_socket->connectToHost(ip, m_failure.address.port);
#endif
}

QcJoinFailure::Step QcTcpConnector::step() const
{
    if (lookupId() >= 0) {
        return QcJoinFailure::Step::Lookup;
    }
    return m_handshaking ? QcJoinFailure::Step::Handshake : QcJoinFailure::Step::Connecting;
}

void QcTcpConnector::noteStep(const QString &what)
{
    const QString line = u"%1: %2"_s.arg(current().toString(), what);
    m_failure.steps.append(line);
    qCInfo(lcConnection).noquote() << u"Connecting to %1 port %2 failed: %3"_s.arg(current().toString()).arg(m_failure.address.port).arg(what);
}

void QcTcpConnector::onSocketError()
{
    const QAbstractSocket::SocketError error = m_socket->error();
    const QString detail = m_socket->errorString();
    const QcJoinFailure::Step at = step();
    noteStep(detail);
    // Another address may answer; once the connection stood, the server is found.
    if (at == QcJoinFailure::Step::Connecting && !m_pending.isEmpty()) {
        dropSocket();
        connectNext();
        return;
    }
    fail(QcJoinFailure::kindOf(error, at), detail);
}

void QcTcpConnector::onTimeout(bool wholeAttempt)
{
    const QcJoinFailure::Step at = step();
    const QString detail = at == QcJoinFailure::Step::Lookup ? u"the name lookup took too long"_s : u"no answer in time"_s;
    if (at != QcJoinFailure::Step::Lookup) {
        noteStep(detail);
    }
    if (!wholeAttempt && !m_pending.isEmpty()) {
        dropSocket();
        connectNext();
        return;
    }
    fail(QcJoinFailure::kindOf(QAbstractSocket::SocketTimeoutError, at), detail);
}

QByteArray QcTcpConnector::fingerprintOf(QTcpSocket *socket)
{
#if QC_TLS_ENABLED
    const QSslCertificate certificate = qobject_cast<QSslSocket *>(socket)->peerCertificate();
    return certificate.isNull() ? QByteArray() : QcTlsFingerprint::of(certificate);
#else
    // Plain TCP: there is nothing to pin.
    Q_UNUSED(socket)
    return {};
#endif
}

void QcTcpConnector::onEstablished()
{
    m_timeout->stop();
    m_addressTimeout->stop();
    QTcpSocket *socket = m_socket;
    const QByteArray fingerprint = fingerprintOf(socket);
    if (QcTlsBackend::enabled && fingerprint.isEmpty()) {
        fail(QcJoinFailure::Kind::NoCertificate, {});
        return;
    }

    m_socket = nullptr;
    socket->disconnect(this);
    m_fingerprint = fingerprint;
    m_transport = std::make_unique<QcTcpTransport>(socket, m_limits);
    emit connected();
}

void QcTcpConnector::fail(QcJoinFailure::Kind kind, const QString &detail)
{
    QcJoinFailure failure = QcJoinFailure::of(kind, m_failure.address, detail);
    failure.tried = m_failure.tried;
    failure.steps = m_failure.steps;
    abort();
    emit failed(failure);
}

void QcTcpConnector::dropSocket()
{
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

void QcTcpConnector::abort()
{
    ++m_attempt;
    m_timeout->stop();
    m_addressTimeout->stop();
    if (lookupId() >= 0) {
        QHostInfo::abortHostLookup(lookupId());
        setLookupId(-1);
    }
    m_pending.clear();
    m_handshaking = false;
    dropSocket();
}

std::unique_ptr<QcTcpTransport> QcTcpConnector::takeTransport()
{
    return std::move(m_transport);
}
