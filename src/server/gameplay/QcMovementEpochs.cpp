// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/gameplay/QcMovementEpochs.h"

#include <algorithm>

void QcMovementEpochs::update(QcMovementEffects effects, quint64 tick)
{
    if (effects == current()) {
        return;
    }
    if (m_past.size() >= maxKept) {
        m_past.removeFirst();
    }
    m_past.append({m_epoch, current(), tick});
    setCurrent(effects);
    ++m_epoch;
}

QcMovementEffects QcMovementEpochs::forInput(quint16 epoch, quint64 tick) const
{
    const auto past = std::ranges::find(m_past, epoch, &Past::epoch);
    return past != m_past.end() && tick - past->endTick <= graceTicks ? past->effects : current();
}
