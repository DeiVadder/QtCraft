#pragma once

#include "QcMessages.h"
#include "QcProtocol.h"

#include <QObject>

#include <functional>
#include <memory>

class QcTransport;

// Message-level view of a transport: encodes outgoing messages, decodes incoming frames and closes
// the link on undecodable input.
class QcConnection : public QObject
{
    Q_OBJECT

public:
    using Handler = std::function<void(QcMessage &&message)>;

    // `remote` is the role of the other end; it decides which message types are accepted.
    QcConnection(std::unique_ptr<QcTransport> transport, QcProtocol::Peer remote, Handler handler,
                 QObject *parent = nullptr);
    ~QcConnection() override;

    void send(const QcMessage &message);
    void close();
    bool isOpen() const;
    // The protocol error that closed the connection, else the transport's reason.
    QString lastError() const;
    qint64 pendingBytes() const;

signals:
    void closed();

private:
    void onFrame(const QByteArray &frame);

    std::unique_ptr<QcTransport> m_transport;
    QcProtocol::Peer m_remote;
    Handler m_handler;
    QString m_lastError;
};
