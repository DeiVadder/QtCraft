// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/commands/QcCommandRegistry.h"

struct QcServerClient;
class QcServerState;

// /xp (decision E-398), for operators: adds to, sets or shows the experience of a player, in points
// (the default) or levels. "/xp add <player> -5 levels" takes levels away. The client hears of the
// change at the end of the next step (QcServerExperience::sendChanges).
class QcExperienceCommands
{
public:
    explicit QcExperienceCommands(QcServerState &state);

    void addTo(QcCommandRegistry &registry);

private:
    void xp(const QcCommandContext &context);
    void add(const QcCommandContext &context, QcServerClient &player, int amount, bool levels) const;
    void set(const QcCommandContext &context, QcServerClient &player, int amount, bool levels) const;
    static void query(const QcCommandContext &context, const QcServerClient &player);

    QcServerState &m_state;
};
