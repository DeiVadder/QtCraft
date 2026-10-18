#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "common/core/QcBlockPos.h"
#include "common/gameplay/QcItemStack.h"
#include "server/entities/QcEntity.h"

#include <QList>
#include <QString>

#include <array>
#include <optional>

// A minecart (decision E-382): rolls along rails with its own momentum (QcMinecartMotion), stands
// where it left the rails, and carries one player, or a container (a chest cart, a hopper cart,
// decision E-386) with its slots. `motion.velocity` is its velocity in the world (along the track
// while on rails); `yaw` turns with the track.
struct QcMinecart : QcEntity
{
    // The kind of every entity of this type (QcEntity::as).
    static constexpr QcEntityKind entityKind = QcEntityKind::Minecart;

    QcMinecart()
        : QcEntity(QcEntityKind::Minecart)
    {
    }

    // The player (entity id) riding it; 0 for none. Not saved: a rider who leaves the world leaves
    // the cart.
    quint32 rider = 0;
    // Where the rider was put last (QcServerMinecarts): a rider found elsewhere was moved by something
    // else (a teleport) and is no longer in the cart.
    QcVec3 seat;
    // The item it drops when broken (the one that placed it); its `cartBlock` says what it carries.
    QString item;
    // The slots of a container cart (as many as its block has); empty for a plain cart.
    QList<QcItemStack> stacks;
    // Hopper carts: pull items; off after a powered activator rail until an unpowered one.
    bool enabled = true;
    // Hopper carts: they rest until this tick after moving an item (not saved).
    quint64 readyAt = 0;

    // Where a cart came to rest on a level rail, at the lower end of a slope that ends there or on the
    // ground off the rails (not saved): while it stays there, without speed, pushes, a rider getting in
    // or a change of that rail or of the cells beyond its track's ends (off the rails: of the cells
    // around it), its tick has nothing to do.
    struct Rest
    {
        QcVec3 position;
        // On a rail: its cell and block. Off the rails: the cell of the cart's feet and air.
        QcBlockPos rail;
        QcBlockId block = qcAirBlockId;
        // The blocks beyond the first and the second end of the track, level with the rail, one
        // higher and one lower: a rail laid there carries a cart on down a slope.
        std::array<QcBlockId, 6> beyond{};
        // Off the rails: the blocks of the columns its body overlaps (at most 2 x 2), from the cell
        // under its feet to the one above them: the ground under it, what flows into it, a rail it
        // would get on.
        std::array<QcBlockId, 12> around{};
        bool offRails = false;

        bool operator==(const Rest &) const = default;
    };
    std::optional<Rest> rest;

    bool operator==(const QcMinecart &) const = default;
};
