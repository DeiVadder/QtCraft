// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"

#include <QString>

/*! \brief A server address as a player types it (decision E-251): "host", "host:port",
    "1.2.3.4:port", "[v6]:port" or a bare IPv6 address; the port defaults to defaultPort. */
struct QcServerAddress
{
    /*! The port of an address that names none, also the port a server listens on by default. */
    static constexpr quint16 defaultPort = 25600;

    /*! The host name or IP address, without brackets. */
    QString host;
    /*! The port. */
    quint16 port = defaultPort;

    /*! Returns the address in \a text (spaces around it ignored), or the reason it is none. */
    [[nodiscard]] static QcResult<QcServerAddress> parse(const QString &text);
    /*! Returns "host:port", with brackets around IPv6 addresses. */
    QString toString() const;
};
