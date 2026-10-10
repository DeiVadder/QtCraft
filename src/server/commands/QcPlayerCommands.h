#pragma once

#include "QcAccessList.h"
#include "QcCommandRegistry.h"

#include <QString>

class QcPendingCommands;
struct QcServerState;

// Player management (decision E-244): the player list, kick, ban and unban, the whitelist,
// operators and teleporting. The lists are QcAccessList; the local player (the host of a single
// player world) cannot be kicked, banned or lose his operator rights.
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

    // Disconnects every online player of that name with `reason`; returns how many. Local players
    // are never removed (false in `hostRefused`).
    int disconnect(const QString &name, const QString &reason, bool &hostRefused);
    // Replies when `name` does not fit into the full list.
    bool refusedAsFull(const QcCommandContext &context, QcAccessList::List list, const QString &name) const;
    // Replies when a list change could not be written.
    static void reportWrite(const QcCommandContext &context, const QcResult<bool> &changed);
    void moveTo(QcServerClient &player, const QcVec3 &position);

    QcServerState &m_state;
    QcPendingCommands &m_pending;
};
