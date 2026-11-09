// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/protocol/QcMessages.h"
#include "server/machines/QcBeacons.h"

class QcBlockScreens;
class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
class QcServerState;

/*! \brief The beacons in the server (decisions E-1142 to E-1144), as in the reference game: their
    screen and what they do every QcBeacon::checkIntervalTicks.

    Using a beacon opens its screen: the player puts a payment item into its slot and chooses a
    primary and, at level 4, a secondary effect (ChooseBeaconEffects); the server checks the
    choice against the beacon's level, takes the item and keeps the effects with the beacon. The
    screen closes when the beacon is gone, out of reach, the player died or became a spectator;
    the slot then goes back.

    Every checkIntervalTicks of the world's clock each beacon of the dimension looks at its beam
    and, with a beam, at its pyramid (QcBeacon::beamAt, QcBeacon::levelAt). A beacon that shines
    gives its effects (QcBeacon::effectsOf) to every living player within its reach and hums;
    one that starts or stops shining sounds so, and the players near one that starts or rises
    hear of it in the adventure guide (QcAdvancementTrigger::BeaconConstructed). Players who see
    a beacon's chunk get its beam whenever it changes (QcMsg::BeaconBeam). */
class QcServerBeacons
{
public:
    QcServerBeacons(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory, QcBlockScreens &screens);
    /*! The closer registered with QcBlockScreens refers to this object. */
    QcServerBeacons(const QcServerBeacons &) = delete;
    QcServerBeacons &operator=(const QcServerBeacons &) = delete;
    ~QcServerBeacons() = default;

    /*! Opens the screen of the beacon at \a pos for \a client. Returns \c false when it is no
        beacon. Any other screen closes; the same beacon again keeps its slot. */
    bool open(QcServerClient &client, QcBlockPos pos);
    /*! Takes the payment of \a client for the effects of \a choice, when the beacon's level
        allows them and its slot holds a payment item; otherwise warns and sends the screen
        again. */
    void choose(QcServerClient &client, const QcMsg::ChooseBeaconEffects &choice);
    /*! Gives the slot back to \a client and forgets the screen; \a notify tells the client (the
        server closed it). */
    void close(QcServerClient &client, bool notify);
    /*! Once per tick of the current dimension: closes the screens that may not stay open and,
        every QcBeacon::checkIntervalTicks, looks at every beacon. */
    void update();
    /*! Looks at every beacon of the current dimension now (update() does it on its schedule). */
    void lookAtAll();

    /*! Returns the state message for the beacon \a client has open in \a state;
        QcServerInventory::send sends it with every inventory state while the beacon is open. */
    [[nodiscard]] static QcMsg::BeaconState stateOf(const QcServerState &state, const QcServerClient &client);
    /*! Returns the beam message of the beacon at \a pos of \a beacons (no sections while it does
        not shine or no beacon stands there). */
    [[nodiscard]] static QcMsg::BeaconBeam beamOf(const QcBeacons &beacons, QcBlockPos pos);

private:
    // Whether the player's beacon still stands within reach and the player lives and acts.
    bool canStayOpen(const QcServerClient &client) const;
    // Whether `choice` may be paid for at the beacon `entry` with `payment`; warns why not.
    bool accepts(const QcServerClient &client, const QcBeacons::Entry &entry, const QcItemStack &payment,
                 const QcMsg::ChooseBeaconEffects &choice) const;
    // What follows a look at `entry` that made `change`: sounds, the adventure guide, the effects,
    // the clients.
    void react(const QcBeacons::Entry &entry, QcBeacons::Entry::Change change);
    // Gives the effects of the shining `entry` to the living players within its reach.
    void giveEffects(const QcBeacons::Entry &entry);
    // Tells the players near `entry`, which began to shine or rose, of its level.
    void noteConstructed(const QcBeacons::Entry &entry);
    // Sends the beacon's state again to the players who have the beacon at `pos` open.
    void refreshScreens(QcBlockPos pos);
    // Sends `beam` to the players who see its chunk.
    void announce(const QcMsg::BeaconBeam &beam);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
    QcBlockScreens &m_screens;
};
