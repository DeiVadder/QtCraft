#pragma once

#include "QcModLoader.h"
#include "QcResult.h"

#include <QObject>
#include <QString>

#include <memory>

class QcClientSession;
class QcNetworkClient;

// qtcraft-headless --join (decision E-270): the control-interface bot joins a server over TCP
// instead of hosting a world in-process. Logs in with the installation key of the data directory,
// the password and the content this process loaded, then hands over the session; every join ends
// with exactly one of joined() and failed().
class QcHeadlessJoin : public QObject
{
    Q_OBJECT

public:
    // Exit codes of a bot that joined over the network (the others: 0 done, 1 bad options or a
    // failed script request, 2 time limit).
    static constexpr int exitJoinFailed = 3;
    static constexpr int exitConnectionLost = 4;

    struct Options
    {
        QString address;
        QString playerName;
        QString password;
        // <data>/player.key is the bot's identity on the server (made on first use).
        QString dataDirectory;
        int viewDistance = 4;
    };

    explicit QcHeadlessJoin(QObject *parent = nullptr);

    // Starts connecting; an error (bad address, damaged key) when it cannot even start.
    [[nodiscard]] QcStatus start(const Options &options, const QcModLoader::Result &content);
    // The session after joined(); null before and once taken.
    std::unique_ptr<QcClientSession> takeSession();

signals:
    // The session is logged in and loads the world; the receiver takes it over with takeSession().
    void joined();
    // The connection could not be made or broke, or the server refused the login.
    void failed(const QString &reason);

private:
    QcNetworkClient *m_client = nullptr;
};
