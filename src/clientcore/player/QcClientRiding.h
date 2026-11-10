// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/session/QcClientEntities.h"
#include "common/physics/QcEntityMotion.h"

#include <QtGlobal>

#include <optional>

/*! \brief The minecart the local player rides (decision E-382).

    The server moves a rider with the cart and names the cart in the player's state; meanwhile
    the client predicts nothing for the player and shows the player on the seat of the cart as
    its mirror shows the cart (interpolated like every entity), so riding is smooth and never
    corrected. Getting out puts the player where the server placed them, which is no correction
    either. */
class QcClientRiding
{
public:
    /*! Returns the entity id of the vehicle the player is in; 0 for none. */
    quint32 vehicle() const { return m_vehicle; }
    /*! Returns whether the player is in a vehicle. */
    bool isRiding() const { return m_vehicle != 0; }
    /*! Returns whether the player rides a minecart: the cart's mirror carries the player
        (seated). */
    bool ridesCart() const { return isRiding() && !drivesBoat(); }
    /*! Returns whether the player drives a boat: the prediction steps the boat and the player
        sits in it. */
    bool drivesBoat() const { return m_vehicle != 0 && m_driving; }
    /*! Takes the \a vehicle of a QcMsg::PlayerState and whether it is a \a boat (the state carries
        the boat's). Returns true when the player got in or out. */
    bool noteVehicle(quint32 vehicle, bool boat);
    /*! Returns the shown player on the seat of the cart as the entity mirror \a entities shows
        it this tick; none while the cart has not arrived yet (the player stays where it is). On
        a creature \a content tells how high its seat is. */
    [[nodiscard]] std::optional<QcEntityMotion> seated(const QcClientEntities &entities, const QcContentRegistry &content) const;

private:
    quint32 m_vehicle = 0;
    bool m_driving = false;
};
