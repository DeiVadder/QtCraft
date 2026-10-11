#pragma once

#include "QcTransport.h"

#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QTimer>

#include <algorithm>
#include <deque>
#include <memory>

// A transport that delays every frame in both directions by `delayMs` plus a random jitter of up to
// `jitterMs` (decision E-257), keeping the order like TCP does: a frame never overtakes an earlier
// one. Wraps the client's end of a link, so both ways see the delay once.
class QcLatencyTransport : public QcTransport
{
    Q_OBJECT

public:
    QcLatencyTransport(std::unique_ptr<QcTransport> inner, int delayMs, int jitterMs, quint32 seed)
        : m_inner(std::move(inner))
        , m_delayMs(delayMs)
        , m_jitterMs(jitterMs)
        , m_random(seed)
    {
        m_clock.start();
        m_timer.setTimerType(Qt::PreciseTimer);
        m_timer.setInterval(1);
        QObject::connect(&m_timer, &QTimer::timeout, this, [this] { deliverDue(); });
        m_timer.start();
        QObject::connect(m_inner.get(), &QcTransport::frameReceived, this, [this](const QByteArray &frame) { schedule(m_incoming, frame); });
        QObject::connect(m_inner.get(), &QcTransport::closed, this, [this] { m_innerClosed = true; });
    }

    void sendFrame(const QByteArray &frame) override { schedule(m_outgoing, frame); }
    void close() override { m_inner->close(); }
    bool isOpen() const override { return m_inner->isOpen(); }

    // The longest one-way delay a frame got so far.
    qint64 largestDelayMs() const { return m_largestDelayMs; }

private:
    struct Pending
    {
        qint64 dueMs = 0;
        qint64 sentMs = 0;
        QByteArray frame;
    };

    void schedule(std::deque<Pending> &queue, const QByteArray &frame)
    {
        const qint64 now = m_clock.elapsed();
        const qint64 jitter = m_jitterMs > 0 ? qint64(m_random.bounded(m_jitterMs + 1)) : 0;
        const qint64 due = std::max(now + m_delayMs + jitter, queue.empty() ? qint64(0) : queue.back().dueMs);
        queue.push_back({due, now, frame});
    }

    void deliverDue()
    {
        const qint64 now = m_clock.elapsed();
        while (!m_outgoing.empty() && m_outgoing.front().dueMs <= now) {
            m_largestDelayMs = std::max(m_largestDelayMs, now - m_outgoing.front().sentMs);
            m_inner->sendFrame(m_outgoing.front().frame);
            m_outgoing.pop_front();
        }
        while (!m_incoming.empty() && m_incoming.front().dueMs <= now) {
            m_largestDelayMs = std::max(m_largestDelayMs, now - m_incoming.front().sentMs);
            const QByteArray frame = m_incoming.front().frame;
            m_incoming.pop_front();
            emit frameReceived(frame);
        }
        if (m_innerClosed && m_incoming.empty() && !m_closedEmitted) {
            m_closedEmitted = true;
            emit closed();
        }
    }

    std::unique_ptr<QcTransport> m_inner;
    int m_delayMs = 0;
    int m_jitterMs = 0;
    QRandomGenerator m_random;
    QElapsedTimer m_clock;
    QTimer m_timer;
    std::deque<Pending> m_outgoing;
    std::deque<Pending> m_incoming;
    qint64 m_largestDelayMs = 0;
    bool m_innerClosed = false;
    bool m_closedEmitted = false;
};
