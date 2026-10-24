#include "network/QcTcpConnector.h"

#include "network/QcSocketErrors.h"
#include "network/tls/QcTlsBackend.h"
#include "network/tls/QcTlsCredentials.h"

#include <QCoreApplication>
#include <QTcpSocket>
#include <QTimer>

#if QC_TLS_ENABLED
#include <QSslConfiguration>
#include <QSslSocket>
#endif

QcTcpConnector::QcTcpConnector(QObject *parent)
    : QObject(parent)
    , m_timeout(new QTimer(this))
{
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this] { finish(QCoreApplication::translate("QcNetwork", "The server did not answer in time")); });
}

QcTcpConnector::~QcTcpConnector() = default;

void QcTcpConnector::connectTo(const QcServerAddress &address, int timeoutMs, const QcTcpTransport::Limits &limits)
{
    abort();
    m_limits = limits;
    m_fingerprint.clear();
    m_handshaking = false;
    if (const QcStatus tls = QcTlsBackend::ensure(); !tls) {
        // Reported like a socket error: after connectTo() returned, unless the attempt was
        // aborted or replaced by then.
        QTimer::singleShot(0, this, [this, attempt = m_attempt, reason = tls.error()] {
            if (attempt == m_attempt) {
                emit failed(reason);
            }
        });
        return;
    }

#if QC_TLS_ENABLED
    auto *socket = new QSslSocket(this);
    QSslConfiguration configuration = QSslConfiguration::defaultConfiguration();
    // TLS 1.3 only: both ends are QtCraft with the same OpenSSL, so no older protocol, no
    // renegotiation and none of the cipher suites of TLS 1.2 are needed.
    configuration.setProtocol(QSsl::TlsV1_3OrLater);
    // The certificate is asked for but not judged by authorities: the caller checks its pin.
    configuration.setPeerVerifyMode(QSslSocket::QueryPeer);
    socket->setSslConfiguration(configuration);
    connect(socket, &QSslSocket::connected, this, [this] { m_handshaking = true; });
    connect(socket, &QSslSocket::sslErrors, this, [socket] { socket->ignoreSslErrors(); });
    connect(socket, &QSslSocket::encrypted, this, [this] { finish({}); });
    m_socket = socket;
#else
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, [this] { finish({}); });
#endif
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this] { finish(errorText()); });
    m_timeout->start(timeoutMs);
#if QC_TLS_ENABLED
    socket->connectToHostEncrypted(address.host, address.port);
#else
    m_socket->connectToHost(address.host, address.port);
#endif
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

QString QcTcpConnector::errorText() const
{
    const QAbstractSocket::SocketError error = m_socket->error();
    // A server of a version before TLS reads the handshake as a frame too large and hangs up.
    if (m_handshaking && (error == QAbstractSocket::SslHandshakeFailedError || error == QAbstractSocket::RemoteHostClosedError)) {
        return QCoreApplication::translate("QcNetwork", "The server did not set up an encrypted connection (does it run the same version of QtCraft?)");
    }
    return QcSocketErrors::describe(error, m_socket->errorString());
}

void QcTcpConnector::abort()
{
    ++m_attempt;
    m_timeout->stop();
    if (m_socket) {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

std::unique_ptr<QcTcpTransport> QcTcpConnector::takeTransport()
{
    return std::move(m_transport);
}

void QcTcpConnector::finish(const QString &error)
{
    if (!m_socket) {
        return;
    }

    m_timeout->stop();
    QTcpSocket *socket = m_socket;
    m_socket = nullptr;
    socket->disconnect(this);
    const QByteArray fingerprint = error.isEmpty() ? fingerprintOf(socket) : QByteArray();
    if (!error.isEmpty() || (QcTlsBackend::enabled && fingerprint.isEmpty())) {
        socket->abort();
        socket->deleteLater();
        emit failed(error.isEmpty() ? QCoreApplication::translate("QcNetwork", "The server showed no certificate") : error);
        return;
    }

    m_fingerprint = fingerprint;
    m_transport = std::make_unique<QcTcpTransport>(socket, m_limits);
    emit connected();
}
