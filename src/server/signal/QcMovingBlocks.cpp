#include "QcMovingBlocks.h"

#include <algorithm>

bool QcMovingBlocks::add(const Moving &moving)
{
    if (m_moving.size() >= m_maxMoving) {
        return false;
    }
    m_moving.append(moving);
    return true;
}

QList<QcBlockPos> QcMovingBlocks::tick(quint64 now)
{
    QList<QcBlockPos> arrived;
    for (const Moving &moving : std::as_const(m_moving)) {
        if (moving.start + moveTicks == now) {
            arrived.append(moving.to);
        }
    }
    m_moving.removeIf([now](const Moving &moving) { return moving.start + moveTicks < now; });
    return arrived;
}

QcVec3 QcMovingBlocks::positionAt(const Moving &moving, quint64 now)
{
    const double progress = std::clamp(double(now - std::min(now, moving.start)) / moveTicks, 0.0, 1.0);
    const QcVec3 from{moving.from.x + 0.5, double(moving.from.y), moving.from.z + 0.5};
    const QcVec3 to{moving.to.x + 0.5, double(moving.to.y), moving.to.z + 0.5};
    return from + (to - from) * progress;
}

bool QcMovingBlocks::isMovingTo(QcBlockPos cell) const
{
    return std::ranges::any_of(m_moving, [cell](const Moving &moving) { return moving.to == cell; });
}
