#pragma once

#include "QcClientEntities.h"
#include "QcEntityMotion.h"

#include <QtGlobal>

// The minecart the local player rides (decision E-382). The server moves a rider with the cart and
// names the cart in the player's state; meanwhile the client predicts nothing for the player and
// shows the player on the seat of the cart as its mirror shows the cart (interpolated like every
// entity), so riding is smooth and never corrected. Getting out puts the player where the server
// placed them, which is no correction either.
class QcClientRiding
{
public:
    quint32 vehicle() const { return m_vehicle; }
    bool isRiding() const { return m_vehicle != 0; }
    // Takes the vehicle of a QcMsg::PlayerState; true when the player got in or out.
    bool noteVehicle(quint32 vehicle);
    // Moves `motion` (the shown player) onto the seat of the cart as the mirror shows it this tick;
    // leaves it where it is while the cart has not arrived yet.
    void follow(QcEntityMotion &motion, const QcClientEntities &entities) const;

private:
    quint32 m_vehicle = 0;
};
