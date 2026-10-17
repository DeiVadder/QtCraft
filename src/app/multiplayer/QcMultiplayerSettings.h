#pragma once

#include "app/multiplayer/QcServerHistory.h"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

// What the multiplayer screens remember (GameController.multiplayer.settings, decision E-258): the
// player name for network games, the recently joined servers and the last choices for opening a
// world to the LAN, stored in the group "multiplayer" of the settings file. Passwords are never
// stored.
class QcMultiplayerSettings : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QString playerName READ playerName WRITE setPlayerName NOTIFY changed FINAL)
    // [{address, name, lastJoined}] newest first; lastJoined as a local date text.
    Q_PROPERTY(QVariantList recentServers READ recentServers NOTIFY changed FINAL)
    Q_PROPERTY(int hostPort READ hostPort NOTIFY changed FINAL)
    Q_PROPERTY(int hostMaxPlayers READ hostMaxPlayers NOTIFY changed FINAL)

public:
    // The server keeps names to this many characters (QcProfileValidation).
    static constexpr qsizetype maxNameLength = 32;
    static constexpr int maxHostPlayers = 64;

    // Reads the settings file at `path`.
    explicit QcMultiplayerSettings(const QString &path, QObject *parent = nullptr);

    QString playerName() const { return m_playerName; }
    // Cleaned like the server does (control characters removed, at most maxNameLength); stored.
    void setPlayerName(const QString &name);
    QVariantList recentServers() const;
    const QcServerHistory &history() const { return m_history; }
    int hostPort() const { return m_hostPort; }
    int hostMaxPlayers() const { return m_hostMaxPlayers; }

    // A join succeeded: the server goes first in the list.
    void rememberServer(const QString &address, const QString &serverName);
    Q_INVOKABLE void forgetServer(const QString &address);
    void rememberHost(int port, int maxPlayers);
    // The name as the server will see it.
    Q_INVOKABLE QString cleanName(const QString &name) const;

signals:
    void changed();

private:
    void save() const;

    QString m_path;
    QString m_playerName;
    QcServerHistory m_history;
    int m_hostPort = 25600;
    int m_hostMaxPlayers = 8;
};
