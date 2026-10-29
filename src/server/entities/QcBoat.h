// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/physics/QcBoatMotion.h"
#include "server/entities/QcEntity.h"

#include <QString>

#include <array>
#include <optional>

// A boat (decision E-920): floats on water, falls without it and moves by the shared boat physics
// (QcBoatState). It carries one player, the driver, and one creature in its front seat. `motion`
// and `yaw` are its own, `spin` how fast it turns (degrees per tick).
struct QcBoat : QcEntity
{
    // The kind of every entity of this type (QcEntity::as).
    static constexpr QcEntityKind entityKind = QcEntityKind::Boat;

    QcBoat()
        : QcEntity(QcEntityKind::Boat)
    {
    }

    // The item it drops when broken (the one that placed it); its `boatBlock` says what it is built of.
    QString item;
    float spin = 0.0f;
    // The player (entity id) driving it and the creature sitting in it; 0 for none. Not saved: a
    // driver who leaves the world leaves the boat, and a creature saved on its seat is picked up
    // again when it comes back (it touches the boat).
    quint32 driver = 0;
    quint32 passenger = 0;
    // The driver's latest input paddled or turned (QcMsg::Rowing; not saved): its paddles row.
    bool rowing = false;
    // Where the driver and the passenger were put last (QcServerBoats): one found elsewhere was
    // moved by something else (a teleport, a knock) and is no longer in the boat.
    QcVec3 driverSeat;
    QcVec3 passengerSeat;
    // Where a boat nobody drives came to rest, still on the water or on the ground (QcBoatManager;
    // not saved): while it stays there without speed, turn, pushes or a rider getting in, and its
    // blocks stay the same, its tick has nothing to do.
    struct Rest
    {
        QcVec3 position;
        float yaw = 0.0f;
        // QcServerWorld::blockChanges() when the blocks were read: a chunk without a later change
        // (QcServerWorld::lastChangeIn) needs no new reading.
        quint64 readAt = 0;
        // The blocks of the columns its body covers (at most 3 x 3), from the cell under its feet to
        // the one with its top (at most 3 high): the water it floats on, the ground under it, what
        // flows in. Cells beyond the body's columns, and those of a chunk not loaded, read as no
        // block.
        std::array<QcBlockId, 27> around{};

        bool operator==(const Rest &) const = default;
    };
    std::optional<Rest> rest;
    // The state the shared physics steps.
    QcBoatState state() const { return {motion, yaw, spin}; }
    void setState(const QcBoatState &state)
    {
        motion = state.motion;
        yaw = state.yaw;
        spin = state.spin;
    }

    bool operator==(const QcBoat &) const = default;
};
