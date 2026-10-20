#pragma once

#include "common/protocol/QcLoginMessages.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

class QcClientSession;

// The players on the server for the player list overlay (GameController.multiplayer.players,
// decision E-259), from the session's QcMsg::PlayerList, with the server's name and message of the
// day. Announces players who join or leave as chat lines. No ping: the protocol measures none.
class QcPlayerList : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    // [{name, operator, self, spectator}] sorted by name without case, spectators last.
    Q_PROPERTY(QVariantList players READ players NOTIFY changed FINAL)
    Q_PROPERTY(int count READ count NOTIFY changed FINAL)
    // The server's name; the world's name for a single-player world (also when open to the LAN).
    Q_PROPERTY(QString serverName READ serverName NOTIFY changed FINAL)
    Q_PROPERTY(QString motd READ motd NOTIFY changed FINAL)

public:
    using Entry = QcMsg::PlayerList::Entry;

    // Names of the players who came and went between two lists, without the own entity.
    struct Changes
    {
        QStringList joined;
        QStringList left;
    };

    explicit QcPlayerList(QObject *parent = nullptr);

    // Follows `session` (null: no session); its first list is taken without announcements.
    void follow(QcClientSession *session);

    QVariantList players() const;
    int count() const { return int(m_entries.size()); }
    QString serverName() const;
    QString motd() const;

    static Changes compare(const QList<Entry> &before, const QList<Entry> &after, quint32 ownEntity);

signals:
    void changed();
    // A translated line for the chat ("Alex joined the game").
    void announced(const QString &line);

private:
    void update();

    QPointer<QcClientSession> m_session;
    QList<Entry> m_entries;
    bool m_received = false;
};
