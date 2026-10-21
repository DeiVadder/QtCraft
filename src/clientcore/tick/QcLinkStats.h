#pragma once

#include "common/diagnostics/QcIntervalStats.h"
#include "common/protocol/QcConnection.h"

#include <QtGlobal>

#include <array>
#include <mutex>
#include <optional>

// The connection as the health line sees it (decision E-834): the round trip of player inputs (from
// sending one until a player state acknowledges it, so it includes up to one server tick) and the
// bytes of the connection. Written by the thread that ticks the session (QcSessionCore), read by
// others; every access takes the mutex.
class QcLinkStats
{
public:
    // Inputs remembered until acknowledged; older ones are forgotten (20 per second).
    static constexpr int remembered = 64;

    struct Taken
    {
        std::optional<QcIntervalTiming> roundTrip;
        QcTraffic traffic;
    };

    void inputSent(quint32 sequence, qint64 nowNs);
    // The server applied the inputs up to `sequence`.
    void acknowledged(quint32 sequence, qint64 nowNs);
    void setTraffic(QcTraffic traffic);
    // Round trips since the previous take() and the traffic so far.
    Taken take();

private:
    struct Sent
    {
        quint32 sequence = 0;
        qint64 atNs = 0;
    };

    mutable std::mutex m_mutex;
    std::array<Sent, remembered> m_sent{};
    std::size_t m_next = 0;
    quint32 m_acknowledged = 0;
    QcIntervalStats m_roundTrip;
    QcTraffic m_traffic;
};
