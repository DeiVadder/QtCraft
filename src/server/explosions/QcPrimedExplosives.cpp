// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/explosions/QcPrimedExplosives.h"

#include "common/core/QcWorldConstants.h"
#include "common/physics/QcPhysics.h"

#include <cmath>

bool QcPrimedExplosives::prime(QcBlockPos cell, QcBlockId block, int fuse, quint32 id, double sideways)
{
    if (isFull()) {
        return false;
    }

    QcPrimedExplosive primed;
    primed.id = id;
    primed.block = block;
    primed.fuse = fuse;
    primed.motion.position = {cell.x + 0.5, static_cast<double>(cell.y), cell.z + 0.5};
    primed.motion.velocity = {std::cos(sideways) * hopSide, hopUp, std::sin(sideways) * hopSide};
    m_primed.append(primed);
    return true;
}

QList<QcPrimedExplosive> QcPrimedExplosives::tick(const QcBlockAccess &world, double dt, qsizetype most)
{
    QList<QcPrimedExplosive> due;
    QList<QcPrimedExplosive> still;
    still.reserve(m_primed.size());

    for (QcPrimedExplosive primed : std::as_const(m_primed)) {
        if (QcWorld::isInVoid(primed.motion.position.y)) {
            continue;
        }
        if (primed.dormant) {
            still.append(primed);
            continue;
        }

        QcEntityMotion &motion = primed.motion;
        motion.inWater = QcPhysics::isLiquidAt(world, QcPhysics::blockContaining(motion.position + QcVec3{0.0, size * 0.5, 0.0}));
        motion.step(QcPhysics::Steering{}, QcPhysics::BodySize{size, size}, world, dt);
        if (QcWorld::isInVoid(motion.position.y)) {
            continue;
        }

        primed.fuse = std::max(0, primed.fuse - 1);
        if (primed.fuse == 0 && due.size() < most) {
            due.append(primed);
        } else {
            still.append(primed);
        }
    }

    m_primed = std::move(still);
    return due;
}

qsizetype QcPrimedExplosives::removeInChunk(QcChunkPos chunk)
{
    return m_primed.removeIf([chunk](const QcPrimedExplosive &primed) { return QcPhysics::blockContaining(primed.motion.position).chunk() == chunk; });
}

QList<QcPrimedExplosive> QcPrimedExplosives::adopt(const QList<QcPrimedExplosive> &primed, const std::function<quint32()> &nextEntityId)
{
    QList<QcPrimedExplosive> left;
    for (QcPrimedExplosive entry : primed) {
        if (isFull()) {
            left.append(entry);
            continue;
        }
        entry.id = nextEntityId();
        entry.settled = false;
        m_primed.append(entry);
    }
    return left;
}

QcFallingBlock QcPrimedExplosives::unlit(const QcPrimedExplosive &primed)
{
    QcFallingBlock falling;
    falling.block = primed.block;
    falling.motion = primed.motion;
    falling.kept = primed.kept;
    return falling;
}
