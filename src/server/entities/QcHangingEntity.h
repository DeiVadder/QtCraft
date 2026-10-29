// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcHanging.h"
#include "common/gameplay/QcItemStack.h"
#include "server/entities/QcEntity.h"

#include <QString>

// An item frame or a painting on a block face (decisions E-790, E-791): no AI, no physics; it hangs
// where it was placed until its wall or its room goes (QcServerHanging::tick). Its position is the
// middle of its front (QcHanging::centre).
struct QcHangingEntity : QcEntity
{
    // The kind of every entity of this type (QcEntity::as).
    static constexpr QcEntityKind entityKind = QcEntityKind::Hanging;

    QcHangingEntity()
        : QcEntity(QcEntityKind::Hanging)
    {
    }

    QcHanging hanging;
    // The item that placed it and that it drops (item frame, painting).
    QString item;
    // Item frames: the one item shown, empty for none.
    QcItemStack held;

    bool operator==(const QcHangingEntity &) const = default;
};
