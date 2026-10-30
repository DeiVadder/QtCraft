// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "network/QcNetworkConfig.h"

#include <QHash>
#include <QHostAddress>
#include <QString>

/*! \brief Bounds new connections per network address (decision E-254): a token bucket per address
    against connection floods, and a cap of open connections per address and in all.

    Pure bookkeeping in the thread of the listener; the times are given by the caller. */
class QcConnectionLimiter
{
public:
    /*! The limits: rate and burst per address, open connections per address and in all. */
    using Limits = QcConnectionLimits;
    /*! Addresses remembered at most; full buckets are forgotten first. */
    static constexpr qsizetype maxTracked = 4096;

    explicit QcConnectionLimiter(const Limits &limits);

    /*! Returns the source the limits count for \a address: an IPv4 address (also mapped into
        IPv6) as it is, an IPv6 address by its /64 network, which one household or server
        typically gets whole (decision E-274). */
    static QString sourceOf(const QHostAddress &address);

    /*! Returns whether a new connection from \a address may open at \a nowMs; counts it when it
        may. */
    bool admit(const QString &address, qint64 nowMs);
    /*! A connection from \a address admitted before has closed. */
    void closed(const QString &address);
    /*! Returns the number of open connections in all. */
    int open() const { return m_open; }
    /*! Returns the number of open connections from \a address. */
    int openFrom(const QString &address) const { return m_openPerAddress.value(address); }

private:
    struct Bucket
    {
        double tokens = 0.0;
        qint64 refilledAt = 0;
    };

    void forgetFullBuckets(qint64 nowMs);

    Limits m_limits;
    QHash<QString, Bucket> m_buckets;
    QHash<QString, int> m_openPerAddress;
    int m_open = 0;
};
