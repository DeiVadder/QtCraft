// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/bosses/QcServerBossBars.h"

#include "server/core/QcServerState.h"

#include <algorithm>
#include <cmath>

namespace {

// Whether `a` and `b` show the same: a client is told only of a change.
bool sameBar(const QcMsg::BossBar &a, const QcMsg::BossBar &b)
{
    return a.kind == b.kind && a.name == b.name && a.colour == b.colour && a.shown == b.shown && std::abs(a.progress - b.progress) < 1e-4f;
}

} // namespace

QcServerBossBars::QcServerBossBars(QcServerState &state)
    : m_state(state)
{
}

QcMsg::BossBar QcServerBossBars::barOf(const QcServerState &state, const QcCreature &mob)
{
    const QcMobDefinition &definition = state.content().mobs()[mob.type];
    const double share = definition.maxHealth > 0.0 ? mob.health / definition.maxHealth : 0.0;
    return {.entity = mob.id,
            .kind = definition.name,
            .name = mob.name,
            .progress = static_cast<float>(std::clamp(share, 0.0, 1.0)),
            .colour = static_cast<quint8>(definition.bossBar ? definition.bossBar->colour : QcBossBarColour::Purple),
            .shown = true};
}

void QcServerBossBars::update(QcServerClient &client) const
{
    const QHash<quint32, QcMsg::BossBar> now = barsFor(client);
    QHash<quint32, QcMsg::BossBar> &shown = client.view.bossBars;
    for (auto it = shown.begin(); it != shown.end();) {
        if (now.contains(it.key())) {
            ++it;
            continue;
        }
        QcMsg::BossBar gone = it.value();
        gone.shown = false;
        client.connection->send(gone);
        it = shown.erase(it);
    }
    for (const QcMsg::BossBar &bar : now) {
        const auto was = shown.constFind(bar.entity);
        if (was == shown.constEnd() || !sameBar(*was, bar)) {
            client.connection->send(bar);
            shown.insert(bar.entity, bar);
        }
    }
}

QHash<quint32, QcMsg::BossBar> QcServerBossBars::barsFor(const QcServerClient &client) const
{
    QHash<quint32, QcMsg::BossBar> bars;
    for (const QcCreature &mob : m_state.entities().creatures().mobs()) {
        const QcMobDefinition &definition = m_state.content().mobs()[mob.type];
        // A showcase's display (without a mind or in its pen) shows none.
        if (!definition.bossBar || mob.noAi || mob.confined || mob.health <= 0.0) {
            continue;
        }
        if ((mob.motion.position - client.motion.position).length() <= definition.bossBar->range) {
            bars.insert(mob.id, barOf(m_state, mob));
        }
    }
    return bars;
}
