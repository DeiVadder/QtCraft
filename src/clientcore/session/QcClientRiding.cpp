#include "clientcore/session/QcClientRiding.h"

#include "common/physics/QcPhysics.h"
#include "common/world/QcMinecartBody.h"

bool QcClientRiding::noteVehicle(quint32 vehicle)
{
    const bool changed = vehicle != m_vehicle;
    m_vehicle = vehicle;
    return changed;
}

void QcClientRiding::follow(QcEntityMotion &motion, const QcClientEntities &entities) const
{
    const auto cart = entities.all().constFind(m_vehicle);
    if (cart == entities.all().constEnd()) {
        return;
    }

    const QcVec3 seat{0.0, QcMinecartBody::seatOffset, 0.0};
    motion.position = cart->position + seat;
    motion.velocity = (cart->position - cart->previousPosition) * QcPhysics::tickRate;
    motion.onGround = true;
    motion.inWater = false;
    motion.flying = false;
    motion.climbing = false;
    motion.slowed = false;
}
