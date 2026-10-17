#pragma once

#include "network/QcServerAddress.h"
#include "network/QcTcpTransport.h"

#include <QObject>

#include <memory>

class QTcpSocket;
class QTimer;

// Opens a TCP connection to a server (name lookup, IPv4 or IPv6) and hands it over as a transport.
// Exactly one of connected() and failed() follows connectTo().
class QcTcpConnector : public QObject
{
    Q_OBJECT

public:
    explicit QcTcpConnector(QObject *parent = nullptr);
    ~QcTcpConnector() override;

    void connectTo(const QcServerAddress &address, int timeoutMs = 10'000, const QcTcpTransport::Limits &limits = {});
    void abort();
    // The open transport after connected(); null before and after taking it.
    std::unique_ptr<QcTcpTransport> takeTransport();

signals:
    void connected();
    void failed(const QString &reason);

private:
    void finish(const QString &error);

    QTcpSocket *m_socket = nullptr;
    QTimer *m_timeout = nullptr;
    QcTcpTransport::Limits m_limits;
    std::unique_ptr<QcTcpTransport> m_transport;
};
