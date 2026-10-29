// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "server/entities/QcEntity.h"

#include <QString>

// Whether a player may pick up a stuck projectile (decision E-425): as its item (shot from a
// survival player's ammunition), only to remove it (shot in creative mode, which used no item), or
// not at all (shot by a creature).
enum class QcProjectilePickup : quint8 { Item, Remove, Never };

// A projectile flying or stuck in a block (decision E-425); QcProjectileManager moves, sticks and
// ages them. Its motion's velocity is the flight; yaw and pitch (degrees) point along it.
struct QcProjectile : QcEntity
{
    // The kind of every entity of this type (QcEntity::as).
    static constexpr QcEntityKind entityKind = QcEntityKind::Projectile;

    QcProjectile()
        : QcEntity(QcEntityKind::Projectile)
    {
    }

    // Name of its QcProjectileDefinition.
    QString type;
    float pitch = 0.0f;
    // Seconds since it was shot, or since it stuck while `stuck`.
    double age = 0.0;
    QcProjectilePickup pickup = QcProjectilePickup::Item;
    // Damage of a hit is multiplied by this (a fully drawn bow: 1; later enchantments).
    double damageFactor = 1.0;
    // The entity that shot it (not saved): it does not hit its shooter while it leaves him.
    quint32 shooter = 0;
    bool stuck = false;
    // The block it sticks in, and that block's state: it falls again when the block changes.
    QcBlockPos stuckIn;
    quint32 stuckState = 0;
    // Never saved (a fishing rod's bobber, decision E-503): left out of the chunk's records
    // (QcEntityRecords), so it goes with its chunk.
    bool transient = false;
};
