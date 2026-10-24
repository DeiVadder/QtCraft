#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

// Frame-based, bidirectional link between one client and the server. Implementations: in-process
// (single player, QcInProcessTransport) and TCP (QcTcpTransport). A transport may be used from the
// thread it lives in only.
class QcTransport : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    virtual void sendFrame(const QByteArray &frame) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    // Where the other end is ("address:port"); empty in-process.
    virtual QString peerAddress() const { return {}; }
    // Bytes sent but not yet handed to the network (0 in-process); senders of bulk data hold back
    // while a slow peer has much waiting.
    virtual qint64 pendingBytes() const { return 0; }
    // Why the transport closed itself (timeout, too slow, broken frame); empty otherwise.
    virtual QString closeReason() const { return {}; }
    // Whether the transport must stay in the thread it was made in (moveToThread() would break it;
    // a TLS socket, decision E-872). Whoever wants it in another thread makes it there.
    virtual bool keepsItsThread() const { return false; }

signals:
    void frameReceived(const QByteArray &frame);
    void closed();
};
