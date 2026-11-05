// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/physics/QcBoatMotion.h"
#include "common/protocol/QcMessages.h"
#include "common/world/QcBlockAccess.h"
#include "server/entities/QcBoat.h"

#include <optional>

class QcServerInventory;
class QcServerSounds;
struct QcCreature;
struct QcServerClient;
class QcServerState;

/*! \brief What players do with boats and how boats move in the server tick (decision E-920):
    placing a boat from its item on water or on the ground, breaking it with a hit (it drops its
    item), getting in with a use and out with sneak, and driving it: each input of the driver steps
    their boat by the shared boat physics (QcBoatState::step) with the input's keys, the same step
    the driver's client predicts, so driving needs no correction.

    In the tick the boats nobody drives move (QcBoatManager), a boat picks up a creature it
    touches into its free front seat, and drivers and passengers are put on their seats. */
class QcServerBoats
{
public:
    /*! A driver or passenger found further than this from the seat was moved by something else
        (a teleport, a knock-back, a respawn) and is no longer in the boat. */
    static constexpr double seatTolerance = 1.0;
    /*! A creature this close to a boat's box (horizontally) touches it and gets in. */
    static constexpr double pickupReach = 0.2;

    QcServerBoats(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory);

    /*! Places a boat from the held item of \a client on the water or ground \a message clicked.
        Returns \c false when the item places no boat. A refused placement (too far, no room,
        limit) still returns true. */
    bool place(QcServerClient &client, const QcMsg::UseItem &message);
    /*! Handles a use of the boat \a id by \a client: the player gets in as its driver. Returns
        \c false when \a id is no boat. */
    bool use(QcServerClient &client, quint32 id);
    /*! Handles a hit of \a client on the boat \a id, which breaks it (dropping its item, except
        in creative mode); its riders get out. Returns \c false when \a id is no boat. */
    bool hit(QcServerClient &client, quint32 id);
    /*! Handles the \a input of the driver \a client: the view turns, sneak gets out, otherwise
        the keys paddle and steer and the boat moves one tick. Returns \c false when the player
        drives no boat (the input moves the player). */
    bool steer(QcServerClient &client, const QcPlayerInput &input);
    /*! Gets the player \a client out of the boat, onto the bank beside it or on top of it;
        nothing when not driving. */
    void leave(QcServerClient &client);
    /*! Adds \a boat (commands, tests) with a new id. Returns it, or null when the limit is
        reached. */
    QcBoat *spawn(QcBoat boat);
    /*! Returns a new boat of the boat item \a item at \a wanted facing \a yaw (/summon), or null
        when the item
        places no boat or the limit is reached. */
    QcBoat *summon(const QString &item, const QcVec3 &wanted, float yaw);
    /*! Moves the boats nobody drives, lets boats pick up creatures and seats drivers and
        passengers. */
    void tick();

    /*! Returns the entity id of the boat the player \a client drives, 0 for none. */
    quint32 vehicleOf(const QcServerClient &client) const;
    /*! Returns the state of the boat the player \a client drives, for their client's prediction;
        none when not driving. */
    [[nodiscard]] std::optional<QcBoatState> stateOf(const QcServerClient &client) const;
    /*! Returns free ground of \a world for a driver getting out of \a boat: beside the boat on a
        bank, else on top of the boat. Also where a driver is saved (QcServerPersistence). */
    [[nodiscard]] static QcVec3 exitSpot(const QcBlockAccess &world, const QcBoat &boat);
    /*! Returns whether \a creature, by its kind, fits into a boat: it walks (no fish, flyer or
        hoverer) and is narrower than the boat. */
    [[nodiscard]] bool fits(const QcCreature &creature) const;

private:
    QcServerClient *driver(const QcBoat &boat) const;
    bool canReach(const QcServerClient &client, const QcBoat &boat) const;
    // Where a boat of `item` is placed for a use on `message.target`: on the water surface of a
    // clicked water cell, on top of a clicked block; none for other clicks.
    [[nodiscard]] std::optional<QcVec3> placeFor(const QcMsg::UseItem &message) const;
    // Puts the driver and the passenger of the boat on their seats.
    void seatRiders(QcBoat &boat) const;
    void seatDriver(QcServerClient &client, QcBoat &boat) const;
    // Riders that are gone, dead or were moved away from their seats leave their boats.
    void releaseLostRiders();
    // Every awake boat with a free front seat takes the first creature (in the table's order) that
    // touches it.
    void pickUpCreatures();
    // Creatures sitting in a boat that is gone or no longer carries them get out.
    void releaseStrandedPassengers();
    // Lets the passenger out of the boat: it stands up and thinks again.
    void releasePassenger(QcBoat &boat) const;

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
};
