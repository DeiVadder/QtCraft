#include "network/tls/QcTlsListener.h"

#include "network/tls/QcTlsBackend.h"

#include <QTcpSocket>

#include <memory>

#if QC_TLS_ENABLED
#include <QSslSocket>
#endif

#ifdef Q_OS_WIN
#include <winsock2.h>
#else
#include <unistd.h>
#endif

namespace {

// Closes an accepted connection that no socket took over (QTcpServer leaves that to the caller).
void closeDescriptor(qintptr descriptor)
{
#ifdef Q_OS_WIN
    ::closesocket(static_cast<SOCKET>(descriptor));
#else
    ::close(static_cast<int>(descriptor));
#endif
}

} // namespace

QcTlsListener::QcTlsListener(const QcTlsCredentials &credentials, QObject *parent)
    : QTcpServer(parent)
{
    // The backend before the first TLS object; a failure shows in QcNetworkServer::listen().
    if (!QcTlsBackend::ensure()) {
        return;
    }
#if QC_TLS_ENABLED
    m_configuration = QSslConfiguration::defaultConfiguration();
    m_configuration.setLocalCertificate(credentials.certificate);
    m_configuration.setPrivateKey(credentials.privateKey);
    // TLS 1.3 only (QcTcpConnector): no renegotiation, no cipher suites of older versions.
    m_configuration.setProtocol(QSsl::TlsV1_3OrLater);
    // Players have no certificates; they prove who they are in the login (decision E-252).
    m_configuration.setPeerVerifyMode(QSslSocket::VerifyNone);
#else
    Q_UNUSED(credentials)
#endif
}

void QcTlsListener::startEncryption(QTcpSocket *socket)
{
#if QC_TLS_ENABLED
    if (auto *secure = qobject_cast<QSslSocket *>(socket)) {
        secure->startServerEncryption();
    }
#else
    Q_UNUSED(socket)
#endif
}

void QcTlsListener::incomingConnection(qintptr descriptor)
{
#if QC_TLS_ENABLED
    auto socket = std::make_unique<QSslSocket>(this);
#else
    auto socket = std::make_unique<QTcpSocket>(this);
#endif
    if (!socket->setSocketDescriptor(descriptor)) {
        closeDescriptor(descriptor);
        return;
    }
#if QC_TLS_ENABLED
    socket->setSslConfiguration(m_configuration);
#endif
    addPendingConnection(socket.release());
}
