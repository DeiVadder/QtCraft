// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcMessages.h"
#include "common/protocol/QcProtocol.h"

#include <QObject>

#include <atomic>
#include <functional>
#include <memory>

class QcTransport;

/*! \brief Bytes of the message frames a connection sent and received so far (health line,
    decision E-834). */
struct QcTraffic
{
    /*! Bytes sent. */
    quint64 sent = 0;
    /*! Bytes received. */
    quint64 received = 0;
};

/*! \brief Message-level view of a transport: encodes outgoing messages, decodes incoming frames and
    closes the link on undecodable input.

    Sends nothing more once the other end said goodbye (a Disconnect): it is closing, and data
    arriving at a closed socket would reset the connection (decision E-904). */
class QcConnection : public QObject
{
    Q_OBJECT

public:
    /*! Receives each decoded message. */
    using Handler = std::function<void(QcMessage &&message)>;

    /*! \a remote is the role of the other end; it decides which message types are accepted. */
    QcConnection(std::unique_ptr<QcTransport> transport, QcProtocol::Peer remote, Handler handler,
                 QObject *parent = nullptr);
    ~QcConnection() override;
    Q_DISABLE_COPY_MOVE(QcConnection)

    /*! Encodes \a message and sends it as one frame; does nothing when the transport is closed or
        the other end said goodbye. */
    void send(const QcMessage &message);
    /*! Closes the transport. */
    void close();
    /*! Returns whether the transport is open. */
    bool isOpen() const;
    /*! Returns the protocol error that closed the connection, else the transport's reason. */
    QString lastError() const;
    /*! Returns the bytes the transport has not yet handed to the network
        (QcTransport::pendingBytes). */
    qint64 pendingBytes() const;
    /*! Returns the link underneath (to move it to another thread together with the connection). */
    QcTransport &transport() const { return *m_transport; }
    /*! Frame bytes so far; safe to read from any thread. */
    QcTraffic traffic() const { return {m_sent.load(std::memory_order_relaxed), m_received.load(std::memory_order_relaxed)}; }

signals:
    /*! The transport closed. */
    void closed();

private:
    void onFrame(const QByteArray &frame);

    std::unique_ptr<QcTransport> m_transport;
    QcProtocol::Peer m_remote;
    Handler m_handler;
    QString m_lastError;
    // The other end sent a Disconnect.
    bool m_peerLeft = false;
    std::atomic<quint64> m_sent{0};
    std::atomic<quint64> m_received{0};
};
