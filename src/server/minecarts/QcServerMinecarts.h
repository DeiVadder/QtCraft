// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/protocol/QcMessages.h"
#include "common/world/QcBlockAccess.h"
#include "server/entities/QcMinecart.h"
#include "server/minecarts/QcMinecartHoppers.h"

#include <optional>

class QcBlockScreens;
class QcServerInventory;
class QcServerSounds;
struct QcServerClient;
class QcServerState;

// What players do with minecarts and how carts move in the server tick (decision E-382): placing a
// cart from its item on a rail, breaking it with a hit (it drops its item and what it carries),
// getting in with a use and out with sneak (a use of a container cart opens its screen instead),
// pushing it along with the forward key while riding, and the tick: carts move (QcMinecartManager),
// their riders go with them and hopper carts and hoppers under carts move items (QcMinecartHoppers). The server moves riders, never their input: a
// riding player's position is the cart's seat, so the client shows the player where it shows the
// cart and predicts nothing while riding.
class QcServerMinecarts
{
public:
    // Speed the forward key adds along the track per second while riding (times the share of the view
    // along the track), up to riderPushTopSpeed along the track; off the rails it adds nothing.
    static constexpr double riderPushAcceleration = 2.0;
    static constexpr double riderPushTopSpeed = 4.0;
    // A rider found further than this from the seat was moved by something else (a teleport, a
    // respawn) and is no longer in the cart.
    static constexpr double seatTolerance = 0.5;

    QcServerMinecarts(QcServerState &state, QcServerSounds &sounds, QcServerInventory &inventory, QcBlockScreens &screens);

    // Places a cart from the held item on the clicked rail; false when the item places no cart. A
    // refused placement (no rail, too far, another cart there, limit) still returns true.
    bool place(QcServerClient &client, const QcMsg::UseItem &message);
    // A use of a cart: the player gets in, or looks into a container cart. False when `id` is no cart.
    bool use(QcServerClient &client, quint32 id);
    // A hit on a cart breaks it (dropping its item, except in creative mode); its rider gets out.
    // False when `id` is no cart.
    bool hit(QcServerClient &client, quint32 id);
    // The player's input while riding: the view turns, sneak gets out, forward pushes the cart along.
    // False when the player rides no cart (the input moves the player as usual).
    bool steer(QcServerClient &client, const QcPlayerInput &input);
    // Gets the player out of the cart and onto free ground beside it; nothing when not riding.
    void leave(QcServerClient &client);
    // Adds a cart (commands, world setup, tests) with a new id; null when the limit is reached.
    QcMinecart *spawn(QcMinecart cart);
    // A cart of the cart item `item` near `wanted` (/summon): on the rail in the cell of `wanted` or
    // the one below, standing along it, else at `wanted` itself, where it falls like a dropped cart.
    // Null when the item places no cart or the limit is reached.
    QcMinecart *summon(const QString &item, const QcVec3 &wanted, float yaw);
    // Moves every cart one tick and takes the riders along, then the items of hopper carts.
    void tick();
    const QcMinecartHoppers &hoppers() const { return m_hoppers; }

    // Where a cart stands on the rail at `cell`: the middle of its track; nothing for other blocks.
    [[nodiscard]] std::optional<QcVec3> restingPlace(QcBlockPos cell) const;
    // The entity id of the cart the player rides, 0 for none.
    quint32 vehicleOf(const QcServerClient &client) const;
    // Free ground for a player getting out of the cart: its own cell, else beside it. Also where a
    // rider is saved (QcServerPersistence), never the seat, which lies inside the block under the rail.
    [[nodiscard]] static QcVec3 exitSpot(const QcBlockAccess &world, const QcMinecart &cart);

private:
    QcServerClient *rider(const QcMinecart &cart) const;
    // The forward key of a rider looking along `yaw` (degrees): pushes the cart along its track.
    void push(QcMinecart &cart, double yaw, double forward) const;
    // Puts the rider on the cart's seat.
    void seat(QcServerClient &client, QcMinecart &cart) const;
    // Riders that are gone, dead or were moved away from the seat leave their carts.
    void releaseLostRiders();
    bool canReach(const QcServerClient &client, const QcMinecart &cart) const;

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcServerInventory &m_inventory;
    QcBlockScreens &m_screens;
    QcMinecartHoppers m_hoppers;
};
