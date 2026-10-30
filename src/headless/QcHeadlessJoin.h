// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"
#include "worlds/QcModLoader.h"

#include <QObject>
#include <QString>

#include <memory>

class QcClientSession;
class QcNetworkClient;

/*! \brief qtcraft-headless --join (decision E-270): the control-interface bot joins a server over
    TCP instead of hosting a world in-process.

    Logs in with the installation key of the data directory, the password and the content this
    process loaded, then hands over the session; every join ends with exactly one of joined() and
    failed(). */
class QcHeadlessJoin : public QObject
{
    Q_OBJECT

public:
    /*! Exit codes of a bot that joined over the network (the others: 0 done, 1 bad options or a
        failed script request, 2 time limit): the join failed, or the server ended the connection.
        \qcshared */
    static constexpr int exitJoinFailed = 3;
    static constexpr int exitConnectionLost = 4;

    /*! \brief What start() joins with. */
    struct Options
    {
        /*! The server: \c {host}, \c {host:port} or \c {[v6]:port}. */
        QString address;
        /*! The bot's player name. */
        QString playerName;
        /*! The server's password; empty for none. */
        QString password;
        /*! <data>/player.key is the bot's identity on the server (made on first use). */
        QString dataDirectory;
        /*! The view distance in chunks the bot asks the server for. */
        int viewDistance = 4;
    };

    explicit QcHeadlessJoin(QObject *parent = nullptr);

    /*! Starts connecting as \a options describe, offering the packs of \a content to the server;
        an error (bad address, damaged key) when it cannot even start. */
    [[nodiscard]] QcStatus start(const Options &options, const QcModLoader::Result &content);
    /*! Returns the session after joined(); null before and once taken. */
    std::unique_ptr<QcClientSession> takeSession();

signals:
    /*! The session is logged in and loads the world; the receiver takes it over with
        takeSession(). */
    void joined();
    /*! The connection could not be made or broke, or the server refused the login; \a reason
        says which. */
    void failed(const QString &reason);

private:
    QcNetworkClient *m_client = nullptr;
};
