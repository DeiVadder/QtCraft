// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcItemStack.h"
#include "server/entities/QcEntity.h"

// A dropped item stack lying in the world (decision E-364); QcItemEntityManager moves, merges and
// ages them.
struct QcItemEntity : QcEntity
{
    // The kind of every entity of this type (QcEntity::as).
    static constexpr QcEntityKind entityKind = QcEntityKind::Item;

    QcItemEntity()
        : QcEntity(QcEntityKind::Item)
    {
    }

    QcItemStack stack;
    double age = 0.0;
    double pickupDelay = 0.0;
    // Lying still on the ground: skipped by physics until the block below changes.
    bool resting = false;
    // Beyond QcServerLimits::itemsAwake in a loaded chunk: frozen and not sent to clients, until
    // older items go.
    bool surplus = false;
};
