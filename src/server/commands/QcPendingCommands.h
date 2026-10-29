// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/commands/QcCommandRegistry.h"

#include <QHash>

#include <functional>

// Destructive commands wait for /confirm (decision E-245): each source (player or console) has at
// most one pending action, a new one replaces it, and it expires after timeoutSeconds of real time
// (the players' time of the server, QcServerClock::playerTimeNs). The action keeps the right its
// command needs: a source that lost it (/deop) cannot confirm. The action receives the source that
// confirms, so nothing refers to a player who has left.
class QcPendingCommands
{
public:
    static constexpr int timeoutSeconds = 30;
    static constexpr qint64 timeoutNs = static_cast<qint64>(timeoutSeconds) * 1'000'000'000;
    using Action = std::function<void(const QcCommandSource &source)>;

    // Keeps `action` for the context's source and tells it how to confirm; `nowNs` is the players'
    // time.
    void ask(const QcCommandContext &context, Action action, qint64 nowNs);
    // Runs the source's pending action; false (after telling why) when there is none, it expired or
    // the source may no longer run its command.
    bool confirm(const QcCommandSource &source, qint64 nowNs);
    // Drops the source's pending action; false when there was none.
    bool cancel(const QcCommandSource &source);
    // A player left or lost his rights: his pending action goes.
    void forget(quint32 sourceId) { m_pending.remove(sourceId); }
    bool hasPending(quint32 sourceId) const { return m_pending.contains(sourceId); }

private:
    struct Pending
    {
        Action action;
        QcPermission needed = QcPermission::Operator;
        qint64 expiresNs = 0;
    };

    QHash<quint32, Pending> m_pending;
};
