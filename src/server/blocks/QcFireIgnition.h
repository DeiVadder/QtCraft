// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/protocol/QcMessages.h"
#include "server/blocks/QcFireRules.h"

#include <functional>

class QcServerFire;
class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
class QcServerState;

/*! \brief What lights fire besides fire itself (decision E-393).

    Every new fire goes through QcServerFire::ignite: a spark of its own with the full guarantees of
    decision E-392 (only in air of a loaded chunk where fire may stand, within the caps of chunk and
    world, with its own spread radius around the spark).

    \list
    \li Flint and steel (an item that \c {ignites}) used on a block face lights the cell in front of
        the face; it wears by one per fire, except in creative mode. Dispensers use it the same way
        (QcDispenserActions).
    \li A block with \c {sparks} (lava) lights, with that chance in 100 per block update of it, one
        air cell right next to it that touches a flammable block. The updates come anyway (something
        next to the lava changed, or it flowed), so the cost follows them and not the size of the
        world; a lava lake at rest lights nothing.
    \li Lightning (decision E-396) lights the struck cell and, picked by dice, a few cells right
        around it: at most \c {sparks} fires per strike, only while fire spreads, each a spark of
        its own.
    \endlist */
class QcFireIgnition
{
public:
    /*! Lights the explosive at a position (QcServerExplosions::light); false when there is none. */
    using Light = std::function<bool(QcBlockPos pos)>;

    QcFireIgnition(QcServerState &state, QcServerFire &fire, QcServerSounds &sounds, QcServerInventory &inventory);
    /*! Sets \a light, with which flint and steel used on an explosive, or a dispenser striking
        at one, lights it (decision E-601). */
    void setLight(Light light) { m_light = std::move(light); }

    /*! Handles a use of the held item of \a client on \c {message.target} (\a message). Returns
        \c false when the held item does not ignite (the use is something else). A use that lights
        nothing (no room, out of reach) still returns true. */
    bool use(QcServerClient &client, const QcMsg::UseItem &message);
    /*! A block update of \a block at \a pos: a block with \c {sparks} may light a cell next to
        it. */
    void blockUpdated(QcBlockPos pos, QcBlockId block);
    /*! Lights fire at \a pos for an igniting item (player or dispenser), or the explosive
        standing there; plays the striking sound. Returns whether something was lit. */
    bool strike(QcBlockPos pos);
    /*! Lightning struck the cell \a pos: lights it and up to \a sparks - 1 cells in the 3 × 3 ×
        3 around it (drawn from the seed, the position and the tick), at most \a sparks fires in
        all and none while fire does not spread (game rule fireSpread, fireSpreadRadius 0).
        Returns the fires lit. */
    int lightning(QcBlockPos pos, int sparks);
    /*! Returns the fire traits of the content's block states. */
    const QcFireRules::Table &table() const { return m_table; }

private:
    QcServerState &m_state;
    QcServerFire &m_fire;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
    QcFireRules::Table m_table;
    Light m_light;
};
