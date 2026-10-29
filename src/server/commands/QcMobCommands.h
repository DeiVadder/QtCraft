// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/commands/QcCommandRegistry.h"

struct QcCreature;
struct QcServerClient;
class QcServerState;

// /mob (decision E-510), for operators: changes a creature the player names by `nearest` (the
// nearest within targetRange), `looking` (the one in the player's view) or its entity id. `noai
// <on|off>` takes its mind away or gives it back (QcCreature::noAi); a creature without a mind is
// kept with the world (QcMobManager::keep) so it stays as it was left, and stays kept when its mind
// comes back.
class QcMobCommands
{
public:
    // How far `nearest` and `looking` reach (metres).
    static constexpr double targetRange = 16.0;

    explicit QcMobCommands(QcServerState &state);

    void addTo(QcCommandRegistry &registry);
    // Takes the mind of `mob` away or gives it back; one without a mind is kept with the world.
    static void setNoAi(QcServerState &state, QcCreature &mob, bool noAi);

private:
    void mob(const QcCommandContext &context);
    QcCreature *target(const QcServerClient &client, const QString &word) const;
    QcCreature *inView(const QcServerClient &client) const;

    QcServerState &m_state;
};
