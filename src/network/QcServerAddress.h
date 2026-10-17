#pragma once

#include "common/core/QcResult.h"

#include <QString>

// A server address as a player types it (decision E-251): "host", "host:port", "1.2.3.4:port",
// "[v6]:port" or a bare IPv6 address; the port defaults to defaultPort.
struct QcServerAddress
{
    static constexpr quint16 defaultPort = 25600;

    QString host;
    quint16 port = defaultPort;

    [[nodiscard]] static QcResult<QcServerAddress> parse(const QString &text);
    // "host:port", with brackets around IPv6 addresses.
    QString toString() const;
};
