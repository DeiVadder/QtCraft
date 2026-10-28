#pragma once

#include "server/boats/QcServerBoats.h"
#include "server/minecarts/QcServerMinecarts.h"

#include <optional>

class QcBlockScreens;
class QcServerInventory;
class QcServerSounds;
class QcServerState;
struct QcServerClient;

// The vehicles of the server, minecarts (decision E-382) and boats (decision E-920), and what the
// server asks of both at once: a hit or a use of an entity, the input of a rider, getting out, the
// tick, which vehicle a player is in.
class QcServerVehicles
{
public:
    QcServerVehicles(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory, QcBlockScreens &screens);

    QcServerMinecarts &minecarts() { return m_minecarts; }
    const QcServerMinecarts &minecarts() const { return m_minecarts; }
    QcServerBoats &boats() { return m_boats; }
    const QcServerBoats &boats() const { return m_boats; }

    // A hit or a use of a cart or boat; false when `id` is neither. A player in a vehicle uses no
    // cart or boat.
    bool hit(QcServerClient &client, quint32 id);
    bool use(QcServerClient &client, quint32 id);
    // The input of a rider or driver; false when the player is in no vehicle (the input moves him).
    bool steer(QcServerClient &client, const QcPlayerInput &input);
    // Gets the player out of whatever he is in.
    void leave(QcServerClient &client);
    // Moves the carts, then the boats.
    void tick();
    // The entity id of the cart or boat the player is in, 0 for none; the state of the boat he
    // drives, none for a cart or no vehicle.
    quint32 vehicleOf(const QcServerClient &client) const;
    [[nodiscard]] std::optional<QcBoatState> boatOf(const QcServerClient &client) const { return boats().stateOf(client); }

private:
    QcServerState &m_state;
    QcServerMinecarts m_minecarts;
    QcServerBoats m_boats;
};
