#pragma once

#include "common/protocol/QcMessages.h"
#include "common/protocol/QcProtocol.h"

#include <QObject>

#include <atomic>
#include <functional>
#include <memory>

class QcTransport;

// Bytes of the message frames a connection sent and received so far (health line, decision E-834).
struct QcTraffic
{
    quint64 sent = 0;
    quint64 received = 0;
};

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
    Q_DISABLE_COPY_MOVE(QcConnection)

    void send(const QcMessage &message);
    void close();
    bool isOpen() const;
    // The protocol error that closed the connection, else the transport's reason.
    QString lastError() const;
    qint64 pendingBytes() const;
    // The link underneath (to move it to another thread together with the connection).
    QcTransport &transport() const { return *m_transport; }
    // Frame bytes so far; safe to read from any thread.
    QcTraffic traffic() const { return {m_sent.load(std::memory_order_relaxed), m_received.load(std::memory_order_relaxed)}; }

signals:
    void closed();

private:
    void onFrame(const QByteArray &frame);

    std::unique_ptr<QcTransport> m_transport;
    QcProtocol::Peer m_remote;
    Handler m_handler;
    QString m_lastError;
    std::atomic<quint64> m_sent{0};
    std::atomic<quint64> m_received{0};
};
