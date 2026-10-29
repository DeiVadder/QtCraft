// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/player/QcClientRiding.h"

#include "common/physics/QcPhysics.h"
#include "common/world/QcMinecartBody.h"

bool QcClientRiding::noteVehicle(quint32 vehicle, bool boat)
{
    const bool changed = vehicle != m_vehicle;
    m_vehicle = vehicle;
    m_driving = vehicle != 0 && boat;
    return changed;
}

std::optional<QcEntityMotion> QcClientRiding::seated(const QcClientEntities &entities) const
{
    const auto cart = entities.all().constFind(m_vehicle);
    if (cart == entities.all().constEnd()) {
        return std::nullopt;
    }

    const QcVec3 seat{0.0, QcMinecartBody::seatOffset, 0.0};
    QcEntityMotion motion;
    motion.position = cart->position + seat;
    motion.velocity = (cart->position - cart->previousPosition) * QcPhysics::tickRate;
    motion.onGround = true;
    return motion;
}
