// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/boats/QcServerBoats.h"
#include "server/minecarts/QcServerMinecarts.h"

#include <optional>

class QcBlockScreens;
class QcServerInventory;
class QcServerSounds;
class QcServerState;
struct QcServerClient;

/*! \brief The vehicles of the server, minecarts (decision E-382) and boats (decision E-920), and
    what the server asks of both at once: a hit or a use of an entity, the input of a rider,
    getting out, the tick, which vehicle a player is in. */
class QcServerVehicles
{
public:
    QcServerVehicles(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory, QcBlockScreens &screens);

    /*! Returns the minecarts. */
    QcServerMinecarts &minecarts() { return m_minecarts; }
    /*! Returns the minecarts. */
    const QcServerMinecarts &minecarts() const { return m_minecarts; }
    /*! Returns the boats. */
    QcServerBoats &boats() { return m_boats; }
    /*! Returns the boats. */
    const QcServerBoats &boats() const { return m_boats; }

    /*! \a client hits the entity \a id. Returns \c false when \a id is neither a cart nor a
        boat. */
    bool hit(QcServerClient &client, quint32 id);
    /*! \a client uses the entity \a id. Returns \c false when \a id is neither a cart nor a
        boat; a player in a vehicle uses no cart or boat. */
    bool use(QcServerClient &client, quint32 id);
    /*! Takes \a input of \a client as a rider or driver. Returns \c false when the player is in
        no vehicle (the input moves him). */
    bool steer(QcServerClient &client, const QcPlayerInput &input);
    /*! Gets the player \a client out of whatever he is in. */
    void leave(QcServerClient &client);
    /*! Moves the carts, then the boats. */
    void tick();
    /*! Returns the entity id of the cart or boat the player \a client is in, 0 for none. */
    quint32 vehicleOf(const QcServerClient &client) const;
    /*! Returns the state of the boat the player \a client drives, none for a cart or no
        vehicle. */
    [[nodiscard]] std::optional<QcBoatState> boatOf(const QcServerClient &client) const { return boats().stateOf(client); }

private:
    QcServerState &m_state;
    QcServerMinecarts m_minecarts;
    QcServerBoats m_boats;
};
