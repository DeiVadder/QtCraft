#pragma once

#include "common/protocol/QcFrameReader.h"
#include "common/protocol/QcProtocol.h"
#include "common/protocol/QcTransport.h"

#include <QElapsedTimer>

class QTcpSocket;
class QTimer;

// A transport over TCP (decision E-251): frames with a length prefix (QcFrameReader), a keep-alive
// frame when nothing was sent for a while, a timeout when nothing arrives, and a bound for the
// bytes waiting to be sent, beyond which a peer counts as too slow and is cut off. Lives in the
// thread of its socket (server thread, the client's session tick thread or GUI thread). The socket
// may be a QSslSocket (decision E-872): frames written before its handshake ends wait and go out
// encrypted. A transport over TLS never changes threads (keepsItsThread()).
class QcTcpTransport : public QcTransport
{
    Q_OBJECT

public:
    struct Limits
    {
        // Frames of the peer larger than this break the connection.
        qint64 maxIncomingFrameBytes = QcProtocol::maxFrameBytes;
        // Bytes waiting to be sent beyond this cut a slow peer off.
        qint64 maxPendingBytes = qint64{64} * 1024 * 1024;
        // A keep-alive frame goes out when nothing was sent for this long.
        int keepAliveMs = 5'000;
        // The connection counts as lost when nothing arrived for this long.
        int idleTimeoutMs = 30'000;
        // How long the goodbye of a closed or destroyed transport may take (decision E-904): its
        // socket sends the queued frames (a reason of a Disconnect) and the end in the background
        // and closes once the peer closed too, at the latest after this time; nothing waits for it.
        // The listener of a network game uses longer (QcNetworkConfig): its players' links may be
        // slow.
        int lingerMs = 500;
    };

    // Takes over a connected socket.
    QcTcpTransport(QTcpSocket *socket, const Limits &limits, QObject *parent = nullptr);
    ~QcTcpTransport() override;
    Q_DISABLE_COPY_MOVE(QcTcpTransport)

    void sendFrame(const QByteArray &frame) override;
    void close() override;
    bool isOpen() const override { return m_open; }
    QString peerAddress() const override { return m_peerAddress; }
    qint64 pendingBytes() const override;
    QString closeReason() const override { return m_closeReason; }
    // True over TLS: Qt's OpenSSL backend keeps the socket's TLS state in an object of its own
    // that stays in the thread the socket was made in (decision E-872).
    bool keepsItsThread() const override;
    void peerSaidGoodbye() override { m_peerLeft = true; }

    // Runs the calling thread's event loop until the sockets lingering in it (destroyed transports
    // still in their goodbye, QcTcpGoodbye) are closed, at most 1 s, and ends the rest; for a thread
    // that is about to end, which would otherwise cut them off at once. Returns at once in a thread
    // without.
    static void finishLingering();

private:
    const Limits &limits() const { return m_limits; }
    QcFrameReader &reader() { return m_reader; }
    void setOpen(bool open) { m_open = open; }
    void setCloseReason(const QString &reason) { m_closeReason = reason; }
    bool peerLeft() const { return m_peerLeft; }
    void onReadyRead();
    void onDisconnected();
    void onTimer();
    // Cuts the connection off at once with `reason`.
    void fail(const QString &reason);
    // Emits closed() once, from the event loop.
    void announceClosed();

    QTcpSocket *m_socket = nullptr;
    QTimer *m_timer = nullptr;
    Limits m_limits;
    QcFrameReader m_reader;
    QElapsedTimer m_lastReceived;
    QElapsedTimer m_lastSent;
    QString m_peerAddress;
    QString m_closeReason;
    bool m_open = true;
    bool m_closedAnnounced = false;
    // The peer's Disconnect arrived (peerSaidGoodbye()).
    bool m_peerLeft = false;
};
