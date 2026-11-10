// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcClientBossBars.h"

#include <algorithm>

bool QcClientBossBars::handle(const QcMessage &message)
{
    if (const auto *bar = std::get_if<QcMsg::BossBar>(&message)) {
        apply(*bar);
        return true;
    }
    if (std::holds_alternative<QcMsg::DimensionChange>(message) && !m_bars.isEmpty()) {
        m_bars.clear();
        emit changed();
    }
    return false;
}

void QcClientBossBars::apply(const QcMsg::BossBar &bar)
{
    const auto it = std::ranges::find(m_bars, bar.entity, &QcMsg::BossBar::entity);
    if (!bar.shown) {
        if (it != m_bars.end()) {
            m_bars.erase(it);
            emit changed();
        }
        return;
    }
    if (it != m_bars.end()) {
        *it = bar;
    } else {
        m_bars.append(bar);
    }
    emit changed();
}
