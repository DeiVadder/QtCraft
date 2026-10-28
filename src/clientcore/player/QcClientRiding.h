#pragma once

#include "clientcore/session/QcClientEntities.h"
#include "common/physics/QcEntityMotion.h"

#include <QtGlobal>

#include <optional>

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
    // Rides a minecart: the cart's mirror carries the player (seated).
    bool ridesCart() const { return isRiding() && !drivesBoat(); }
    // Drives a boat: the prediction steps the boat and the player sits in it.
    bool drivesBoat() const { return m_vehicle != 0 && m_driving; }
    // Takes the vehicle of a QcMsg::PlayerState and whether it is a boat (the state carries the
    // boat's); true when the player got in or out.
    bool noteVehicle(quint32 vehicle, bool boat);
    // The shown player on the seat of the cart as the mirror shows it this tick; none while the cart
    // has not arrived yet (the player stays where it is).
    [[nodiscard]] std::optional<QcEntityMotion> seated(const QcClientEntities &entities) const;

private:
    quint32 m_vehicle = 0;
    bool m_driving = false;
};
