// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/signal/QcMovingBlocks.h"

#include <algorithm>

bool QcMovingBlocks::add(const QcMovingBlock &moving)
{
    if (m_moving.size() >= m_maxMoving) {
        return false;
    }
    m_moving.append(moving);
    m_moving.last().motion.position = positionAt(moving, moving.start);
    return true;
}

QList<QcBlockPos> QcMovingBlocks::tick(quint64 now)
{
    QList<QcBlockPos> arrived;
    for (const QcMovingBlock &moving : std::as_const(m_moving)) {
        if (moving.start + moveTicks == now) {
            arrived.append(moving.to);
        }
    }
    m_moving.removeIf([now](const QcMovingBlock &moving) { return moving.start + moveTicks < now; });
    for (QcMovingBlock &moving : m_moving) {
        moving.motion.position = positionAt(moving, now);
    }
    return arrived;
}

QcVec3 QcMovingBlocks::positionAt(const QcMovingBlock &moving, quint64 now)
{
    const double progress = std::clamp(static_cast<double>(now - std::min(now, moving.start)) / moveTicks, 0.0, 1.0);
    const QcVec3 from{moving.from.x + 0.5, static_cast<double>(moving.from.y), moving.from.z + 0.5};
    const QcVec3 to{moving.to.x + 0.5, static_cast<double>(moving.to.y), moving.to.z + 0.5};
    return from + (to - from) * progress;
}

bool QcMovingBlocks::isMovingTo(QcBlockPos cell) const
{
    return std::ranges::any_of(m_moving, [cell](const QcMovingBlock &moving) { return moving.to == cell; });
}

void QcMovingBlocks::cancelTo(QcBlockPos cell)
{
    m_moving.removeIf([cell](const QcMovingBlock &moving) { return moving.to == cell; });
}
