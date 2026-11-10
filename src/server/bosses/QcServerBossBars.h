// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcBossBarMessages.h"

#include <QHash>

class QcServerState;
struct QcCreature;
struct QcServerClient;

/*! \brief The boss bars (decision E-1127): a client sees the bar of every creature of its
    dimension with a QcMobDefinition::bossBar within its QcBossBarDefinition::range (a showcase's
    display, without a mind or confined to its pen, has none), told by QcMsg::BossBar when it
    comes, changes or goes; part of QcEntitySync. What a client was told is in
    QcServerClient::View::bossBars. */
class QcServerBossBars
{
public:
    explicit QcServerBossBars(QcServerState &state);

    /*! Sends \a client, in the current dimension, the boss bars that came, changed or went. */
    void update(QcServerClient &client) const;

    /*! Returns the bar of \a mob of the content of \a state as a client would be told it. */
    [[nodiscard]] static QcMsg::BossBar barOf(const QcServerState &state, const QcCreature &mob);

private:
    // The bars `client` is to see now.
    QHash<quint32, QcMsg::BossBar> barsFor(const QcServerClient &client) const;

    QcServerState &m_state;
};
