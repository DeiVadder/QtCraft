#pragma once

#include "common/protocol/QcProtocol.h"
#include "common/protocol/QcTransport.h"

#include <QList>
#include <QMutex>
#include <QMutexLocker>

#include <memory>

// A client's transport that notes the messages the client sends, in the order they go out, before
// it passes them on: what reaches the server, seen from the test's thread while the client sends from
// its session's tick thread (decision E-560). Reading is safe from any thread.
class QcRecordingTransport : public QcTransport
{
    Q_OBJECT

public:
    explicit QcRecordingTransport(std::unique_ptr<QcTransport> inner)
        : m_inner(std::move(inner))
    {
        // A child follows the transport into the session's tick thread; it leaves its parent again
        // when the member is destroyed, before it.
        m_inner->setParent(this);
        QObject::connect(m_inner.get(), &QcTransport::frameReceived, this, &QcTransport::frameReceived);
        QObject::connect(m_inner.get(), &QcTransport::closed, this, &QcTransport::closed);
    }

    void sendFrame(const QByteArray &frame) override
    {
        if (QcResult<QcMessage> message = QcProtocol::decode(frame, QcProtocol::Peer::Client)) {
            const QMutexLocker lock(&m_mutex);
            m_sent.append(std::move(*message));
        }
        m_inner->sendFrame(frame);
    }

    void close() override { m_inner->close(); }
    bool isOpen() const override { return m_inner->isOpen(); }
    QString closeReason() const override { return m_inner->closeReason(); }

    // The messages sent so far.
    QList<QcMessage> sent() const
    {
        const QMutexLocker lock(&m_mutex);
        return m_sent;
    }

    // The messages of type T sent so far.
    template<typename T>
    QList<T> sentOf() const
    {
        QList<T> result;
        const QList<QcMessage> all = sent();
        for (const QcMessage &message : all) {
            if (const auto *typed = std::get_if<T>(&message)) {
                result.append(*typed);
            }
        }
        return result;
    }

private:
    std::unique_ptr<QcTransport> m_inner;
    mutable QMutex m_mutex;
    QList<QcMessage> m_sent;
};
