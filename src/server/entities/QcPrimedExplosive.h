// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "server/entities/QcEntity.h"

// A lit explosive counting down to its explosion (decision E-601); QcPrimedExplosives moves it like
// a falling block and counts its fuse down, QcServerExplosions sets it off.
struct QcPrimedExplosive : QcEntity
{
    // The kind of every entity of this type (QcEntity::as).
    static constexpr QcEntityKind entityKind = QcEntityKind::PrimedExplosive;

    QcPrimedExplosive()
        : QcEntity(QcEntityKind::PrimedExplosive)
    {
    }

    // The explosive block it was lit from: its power and whether it sets fires, and its look.
    QcBlockId block = qcAirBlockId;
    // Ticks left until it explodes; 0 while it waits for a free place among the explosions of a tick.
    int fuse = 0;
};
