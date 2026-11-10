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

std::optional<QcEntityMotion> QcClientRiding::seated(const QcClientEntities &entities, const QcContentRegistry &content) const
{
    const auto cart = entities.all().constFind(m_vehicle);
    if (cart == entities.all().constEnd()) {
        return std::nullopt;
    }

    // On a creature its kind says how high the seat is (decision E-1054).
    const QcMobDefinition *creature = cart->creature(content);
    const QcVec3 seat{0.0, creature && creature->ride ? creature->ride->seat : QcMinecartBody::seatOffset, 0.0};
    QcEntityMotion motion;
    motion.position = cart->position + seat;
    motion.velocity = (cart->position - cart->previousPosition) * QcPhysics::tickRate;
    motion.onGround = true;
    return motion;
}
