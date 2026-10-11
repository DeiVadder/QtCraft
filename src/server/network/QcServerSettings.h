#pragma once

#include "QcNetworkServer.h"
#include "QcResult.h"
#include "QcServerConfig.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

// The settings of a dedicated server (decision E-255), kept in server.json in the data directory
// (or a file given with --server-config) and overridden by command line options: name, message of
// the day, port and bind address, player limit, password, world, view distance limit and the
// connection limits. Unknown keys are kept out; wrong values are refused with the key's name.
struct QcServerSettings
{
    static constexpr QStringView fileName = u"server.json";
    static constexpr int formatVersion = 1;
    static constexpr int maxViewDistance = 32;
    static constexpr int maxPlayersLimit = 1000;

    QString serverName = QStringLiteral("QtCraft server");
    QString motd;
    quint16 port = 25600;
    // Empty: IPv4 and IPv6 on every interface.
    QString bindAddress;
    int maxPlayers = 8;
    QString password;
    // Folder of the world below <data>/worlds; made with `seed` (none: a random one) and
    // `worldType` when missing.
    QString world = QStringLiteral("World");
    std::optional<qint64> seed;
    QString worldType = QStringLiteral("terrain");
    int viewDistance = 8;
    int loginTimeoutSeconds = 10;
    double connectionsPerSecond = 1.0;
    int connectionBurst = 8;
    int maxConnectionsPerAddress = 8;
    // Operators besides the world's access.json (by name; bound to the identity at their login).
    QStringList operators;

    // Reads a settings file; a missing file gives the defaults.
    static QcResult<QcServerSettings> read(const QString &path);
    static QcResult<QcServerSettings> fromJson(const QJsonObject &json);
    QJsonObject toJson() const;
    // Writes the file (with every key, so it documents itself).
    QcStatus write(const QString &path) const;

    // The listener's address: the bind address, or Any.
    QcResult<QHostAddress> address() const;
    // The values that concern the game server and the listener.
    void applyTo(QcServerConfig &config) const;
    QcNetworkConfig networkConfig() const;
};
