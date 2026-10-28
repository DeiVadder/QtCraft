#include "server/core/QcServerVehicles.h"

#include "server/core/QcServerState.h"

QcServerVehicles::QcServerVehicles(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory, QcBlockScreens &screens)
    : m_state(state)
    , m_minecarts(state, sounds, inventory, screens)
    , m_boats(state, sounds, inventory)
{
}

bool QcServerVehicles::hit(QcServerClient &client, quint32 id)
{
    return minecarts().hit(client, id) || boats().hit(client, id);
}

bool QcServerVehicles::use(QcServerClient &client, quint32 id)
{
    // One in a vehicle gets into no other and opens no cart: the use of a cart or boat is refused.
    if (vehicleOf(client) != 0) {
        return m_state.entities().minecarts().find(id) || m_state.entities().boats().find(id);
    }
    return minecarts().use(client, id) || boats().use(client, id);
}

bool QcServerVehicles::steer(QcServerClient &client, const QcPlayerInput &input)
{
    return minecarts().steer(client, input) || boats().steer(client, input);
}

void QcServerVehicles::leave(QcServerClient &client)
{
    minecarts().leave(client);
    boats().leave(client);
}

void QcServerVehicles::tick()
{
    minecarts().tick();
    boats().tick();
}

quint32 QcServerVehicles::vehicleOf(const QcServerClient &client) const
{
    const quint32 boat = boats().vehicleOf(client);
    return boat != 0 ? boat : minecarts().vehicleOf(client);
}
