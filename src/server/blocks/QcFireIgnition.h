#pragma once

#include "QcBlockPos.h"
#include "QcFireRules.h"
#include "QcMessages.h"

class QcServerFire;
class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
class QcServerState;

// What lights fire besides fire itself (decision E-393). Every new fire goes through
// QcServerFire::ignite: a spark of its own with the full guarantees of decision E-392 (only in air
// of a loaded chunk where fire may stand, within the caps of chunk and world, with its own spread
// radius around the spark).
// - Flint and steel (an item that `ignites`) used on a block face lights the cell in front of the
//   face; it wears by one per fire, except in creative mode. Dispensers use it the same way
//   (QcDispenserActions).
// - A block with `sparks` (lava) lights, with that chance in 100 per block update of it, one air
//   cell right next to it that touches a flammable block. The updates come anyway (something next
//   to the lava changed, or it flowed), so the cost follows them and not the size of the world; a
//   lava lake at rest lights nothing.
class QcFireIgnition
{
public:
    QcFireIgnition(QcServerState &state, QcServerFire &fire, QcServerSounds &sounds, QcServerInventory &inventory);

    // A use of the held item on `message.target`: false when the held item does not ignite (the use
    // is something else). A use that lights nothing (no room, out of reach) still returns true.
    bool use(QcServerClient &client, const QcMsg::UseItem &message);
    // A block update of `block` at `pos`: a block with `sparks` may light a cell next to it.
    void blockUpdated(QcBlockPos pos, QcBlockId block);
    // Lights fire at `pos` for an igniting item (player or dispenser); plays the striking sound.
    bool strike(QcBlockPos pos);
    const QcFireRules::Table &table() const { return m_table; }

private:
    QcServerState &m_state;
    QcServerFire &m_fire;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
    QcFireRules::Table m_table;
};
