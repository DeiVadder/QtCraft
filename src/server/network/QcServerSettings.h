// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"
#include "server/core/QcServerConfig.h"
#include "server/network/QcNetworkServer.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>

/*! \brief The settings of a dedicated server (decision E-255), kept in server.json in the data
    directory (or a file given with --server-config) and overridden by command line options: name,
    message of the day, port and bind address, player limit, password, world, view distance limit
    and the connection limits.

    Unknown keys are kept out; wrong values are refused with the key's name. */
struct QcServerSettings
{
    /*! The name of the settings file in the data directory. */
    static constexpr QStringView fileName = u"server.json";
    /*! The format written into the file ("format"). */
    static constexpr int formatVersion = 1;
    /*! The highest view distance limit in chunks. */
    static constexpr int maxViewDistance = 32;
    /*! The highest player limit. */
    static constexpr int maxPlayersLimit = 1000;
    /*! The longest pause when empty (one day). */
    static constexpr int maxPauseWhenEmptySeconds = 86'400;

    /*! The server's name, sent to clients when they log in. */
    QString serverName = QStringLiteral("QtCraft server");
    /*! The message of the day. */
    QString motd;
    /*! The TCP port to listen on (0: a free one). */
    quint16 port = 25600;
    /*! Empty: IPv4 and IPv6 on every interface. */
    QString bindAddress;
    /*! Players at most (1 to maxPlayersLimit). */
    int maxPlayers = 8;
    /*! The password players must give; empty: none. */
    QString password;
    /*! Folder of the world below <data>/worlds; made with \c {seed} (none: a random one),
        \c {worldType} and the world generation options \c {worldOptions} (decision E-917, as on
        the command line: "preset=more_villages,caves=off"; empty: the defaults) when missing.
        \qcshared */
    QString world = QStringLiteral("World");
    std::optional<qint64> seed;
    QString worldType = QStringLiteral("terrain");
    QString worldOptions;
    /*! The longest world generation options accepted. */
    static constexpr qsizetype maxWorldOptionsLength = 2048;
    /*! The world generator version of a world the server creates (decision E-922); 0, the
        default: the current one. A saved world keeps its own. */
    int generatorVersion = 0;
    /*! The view distance limit in chunks (1 to maxViewDistance). */
    int viewDistance = 8;
    /*! Seconds a connection may take to log in (1 to 600). */
    int loginTimeoutSeconds = 10;
    /*! Seconds without anyone connected before the server stops ticking (decision E-877); 0
        never. */
    int pauseWhenEmptySeconds = 60;
    /*! New connections per second and address in the long run (token bucket, decision E-254). */
    double connectionsPerSecond = 1.0;
    /*! New connections an address may open at once (the bucket's size). */
    int connectionBurst = 8;
    /*! Open connections per address at most. */
    int maxConnectionsPerAddress = 8;
    /*! Operators besides the world's access.json (by name; bound to the identity at their
        login). */
    QStringList operators;
    /*! Game mode of players who join for the first time ("survival", "creative"; empty in the
        file: the world's default). A world the server creates gets it as its default (decision
        E-282). */
    std::optional<QcGameMode> defaultGameMode;
    /*! Budgets of the simulation ("limits", decision E-349), clamped into their ranges; what was
        clamped or unknown is in \c {limitWarnings} for the log. \qcshared */
    QcServerLimits limits;
    QStringList limitWarnings;
    /*! The file read() read (or would have read); not written into it. /limits writes changed
        limits there (QcServerConfig::settingsFile, decision E-848). */
    QString path;

    /*! Reads the settings file \a path; a missing file gives the defaults. Returns the settings
        or the first wrong value. */
    [[nodiscard]] static QcResult<QcServerSettings> read(const QString &path);
    /*! Returns the settings read from \a json (keys missing: defaults), or the first wrong value
        by its key. */
    [[nodiscard]] static QcResult<QcServerSettings> fromJson(const QJsonObject &json);
    /*! Returns the settings as the file's JSON, with every key. */
    QJsonObject toJson() const;
    /*! Writes the file \a target (with every key, so it documents itself). */
    [[nodiscard]] QcStatus write(const QString &target) const;

    /*! Returns the listener's address: the bind address, or Any. */
    [[nodiscard]] QcResult<QHostAddress> address() const;
    /*! Returns the generator version a new world gets: \c {generatorVersion}, or the current one
        for 0. */
    [[nodiscard]] int newWorldGeneratorVersion() const { return generatorVersion == 0 ? QcGeneratorVersion::current : generatorVersion; }
    /*! Returns the values that concern the game server and the listener (the world comes from
        elsewhere). */
    [[nodiscard]] QcServerConfig serverConfig() const;
    /*! Returns the values of the listener: address, port and connection limits (open
        connections in all: the larger of maxPlayers + 16 and twice maxPlayers). */
    QcNetworkConfig networkConfig() const;
};
