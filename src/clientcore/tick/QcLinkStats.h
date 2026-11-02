// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/diagnostics/QcIntervalStats.h"
#include "common/protocol/QcConnection.h"

#include <QtGlobal>

#include <array>
#include <mutex>
#include <optional>

/*! \brief The connection as the health line sees it (decision E-834): the round trip of player
    inputs (from sending one until a player state acknowledges it, so it includes up to one server
    tick) and the bytes of the connection.

    Written by the thread that ticks the session (QcSessionCore), read by others; every access
    takes the mutex. */
class QcLinkStats
{
public:
    /*! Inputs remembered until acknowledged; older ones are forgotten (20 per second). */
    static constexpr int remembered = 64;

    /*! \brief What take() hands out. */
    struct Taken
    {
        /*! The round trips since the previous take(); nothing without one. */
        std::optional<QcIntervalTiming> roundTrip;
        /*! The bytes of the connection so far. */
        QcTraffic traffic;
    };

    /*! The input numbered \a sequence was sent at \a nowNs. */
    void inputSent(quint32 sequence, qint64 nowNs);
    /*! The server applied the inputs up to \a sequence, noted at \a nowNs. */
    void acknowledged(quint32 sequence, qint64 nowNs);
    /*! Takes the bytes of the connection so far, \a traffic. */
    void setTraffic(QcTraffic traffic);
    /*! Returns the round trips since the previous take() and the traffic so far. */
    Taken take();

private:
    struct Sent
    {
        quint32 sequence = 0;
        qint64 atNs = 0;
    };

    // The session's tick thread hands the statistics to the health line's thread under this mutex.
    mutable std::mutex m_mutex;
    std::array<Sent, remembered> m_sent{};
    std::size_t m_next = 0;
    quint32 m_acknowledged = 0;
    QcIntervalStats m_roundTrip;
    QcTraffic m_traffic;
};
