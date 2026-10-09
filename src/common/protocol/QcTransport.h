#pragma once

#include <QByteArray>
#include <QObject>

// Frame-based, bidirectional link between one client and the server. Implementations: in-process
// (single player) and later a socket. A transport may be used from the thread it lives in only.
class QcTransport : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    virtual void sendFrame(const QByteArray &frame) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;

signals:
    void frameReceived(const QByteArray &frame);
    void closed();
};
