#include "network/QcTcpConnector.h"

#include "network/QcSocketErrors.h"

#include <QCoreApplication>
#include <QTcpSocket>
#include <QTimer>

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
    m_socket = new QTcpSocket(this);
    connect(m_socket, &QTcpSocket::connected, this, [this] { finish({}); });
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this] { finish(QcSocketErrors::describe(m_socket->error(), m_socket->errorString())); });
    m_timeout->start(timeoutMs);
    m_socket->connectToHost(address.host, address.port);
}

void QcTcpConnector::abort()
{
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
    if (!error.isEmpty()) {
        socket->abort();
        socket->deleteLater();
        emit failed(error);
        return;
    }

    m_transport = std::make_unique<QcTcpTransport>(socket, m_limits);
    emit connected();
}
