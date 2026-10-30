// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

/*! \brief Frame-based, bidirectional link between one client and the server.

    Implementations: in-process (single player, QcInProcessTransport) and TCP (QcTcpTransport). A
    transport may be used from the thread it lives in only. */
class QcTransport : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    /*! Sends \a frame to the other end. */
    virtual void sendFrame(const QByteArray &frame) = 0;
    /*! Closes the link. */
    virtual void close() = 0;
    /*! Returns whether the link is open. */
    virtual bool isOpen() const = 0;
    /*! Where the other end is ("address:port"); empty in-process. */
    virtual QString peerAddress() const { return {}; }
    /*! Bytes sent but not yet handed to the network (0 in-process); senders of bulk data hold
        back while a slow peer has much waiting. */
    virtual qint64 pendingBytes() const { return 0; }
    /*! Why the transport closed itself (timeout, too slow, broken frame); empty otherwise. */
    virtual QString closeReason() const { return {}; }
    /*! Returns whether the transport must stay in the thread it was made in (moveToThread()
        would break it; a TLS socket, decision E-872). Whoever wants it in another thread makes
        it there. */
    virtual bool keepsItsThread() const { return false; }
    /*! The other end said goodbye (its Disconnect arrived): this end may close at once and send
        its end without waiting for the other's (decision E-904). */
    virtual void peerSaidGoodbye() {}

signals:
    /*! A whole \a frame arrived from the other end. */
    void frameReceived(const QByteArray &frame);
    /*! The link closed. */
    void closed();
};
