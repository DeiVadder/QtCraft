#pragma once

#include "server/commands/QcAccessList.h"
#include "server/commands/QcCommandRegistry.h"

#include <QString>

class QcPendingCommands;
class QcServerState;

// Player management (decision E-244): the player list, kick, ban and unban, the whitelist,
// operators and teleporting. The lists are QcAccessList, bound to the identity of a player when it
// is known (decision E-253); the local player (the host of a single player world) cannot be
// kicked, banned or lose his operator rights.
class QcPlayerCommands
{
public:
    QcPlayerCommands(QcServerState &state, QcPendingCommands &pending);

    void addTo(QcCommandRegistry &registry);

private:
    void addListCommands(QcCommandRegistry &registry);

    void list(const QcCommandContext &context) const;
    void kick(const QcCommandContext &context);
    void ban(const QcCommandContext &context);
    void unban(const QcCommandContext &context);
    void banList(const QcCommandContext &context) const;
    void whitelist(const QcCommandContext &context);
    void op(const QcCommandContext &context, bool grant);
    void teleport(const QcCommandContext &context);

    // What disconnect() did: how many players it removed, and whether one of that name is the
    // world's host, which is never removed.
    struct Disconnected
    {
        int count = 0;
        bool hostRefused = false;
    };

    // Disconnects every online player of that name with `reason`, except local players.
    Disconnected disconnect(const QString &name, const QcMsg::Disconnect &reason);
    // The fingerprint of the player's identity: of him online, else the registered one, else empty.
    QString fingerprintOf(const QString &name) const;
    // Replies when `name` does not fit into the full list.
    bool refusedAsFull(const QcCommandContext &context, QcAccessList::List list, const QString &name) const;
    // Replies when a list change could not be written.
    static void reportWrite(const QcCommandContext &context, const QcResult<bool> &changed);
    void moveTo(QcServerClient &player, const QcVec3 &position);

    QcServerState &m_state;
    QcPendingCommands &m_pending;
};
