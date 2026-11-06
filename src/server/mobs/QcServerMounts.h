// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcVec3.h"

#include <QtGlobal>

class QcBlockAccess;
struct QcServerClient;
class QcServerInventory;
class QcServerSounds;
class QcServerState;
struct QcCreature;
struct QcPlayerInput;

/*! \brief Creatures players ride (QcMobDefinition::ride, decision E-1054; the lava wader): saddling
    (which keeps the creature with the world) and mounting with the use button, the rider's input,
    and the tick that keeps him on the seat.

    The server moves riders like those of minecarts: a rider's position is the seat, the client
    shows him where it shows the creature and predicts nothing. Where the creature goes decides
    its brain (QcDrifterBrain), which reads the rider's look and held item. */
class QcServerMounts
{
public:
    QcServerMounts(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory);

    /*! \a client uses the entity \a id: the saddle item saddles a creature that takes one, using a
        saddled one mounts it. Returns \c false when \a id is no such creature or neither applies,
        so the other uses of creatures may go on. */
    bool use(QcServerClient &client, quint32 id);
    /*! Takes \a input of a rider \a client: the view turns, sneak gets off. Returns \c false when
        he rides nothing. */
    bool steer(QcServerClient &client, const QcPlayerInput &input);
    /*! Gets the player \a client off whatever he rides, at exitSpot(). */
    void leave(QcServerClient &client);
    /*! Keeps the riders on their seats, lets go of those who left and tells those whose creature
        is gone. */
    void tick();
    /*! Returns the entity id of the creature \a client rides, 0 for none. */
    [[nodiscard]] quint32 vehicleOf(const QcServerClient &client) const;
    /*! Returns where a rider of \a mob gets off in \a world (leave(), the saved position of one
        who logs out in the saddle): the nearest ground within exitReach cells that carries him,
        else the nearest open place by the seat, which over a wide lava sea is above the lava. */
    [[nodiscard]] static QcVec3 exitSpot(const QcBlockAccess &world, const QcCreature &mob);

    /*! How far to the side exitSpot() looks for ground, in cells. */
    static constexpr int exitReach = 8;

private:
    // Saddles `mob` when `client` holds the saddle of its kind; returns whether he did.
    bool saddle(QcServerClient &client, QcCreature &mob);
    void seat(QcServerClient &client, const QcCreature &mob) const;

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
};
