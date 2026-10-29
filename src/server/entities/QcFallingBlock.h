// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "server/entities/QcEntity.h"

#include <optional>

// A block falling while nothing holds it (decision E-055, E-364); QcFallingBlockManager lets it fall
// and land.
struct QcFallingBlock : QcEntity
{
    // The kind of every entity of this type (QcEntity::as).
    static constexpr QcEntityKind entityKind = QcEntityKind::FallingBlock;

    QcFallingBlock()
        : QcEntity(QcEntityKind::FallingBlock)
    {
    }

    QcBlockId block = qcAirBlockId;
    double age = 0.0;
    // The height it started falling from (an anvil's landing damage, decision E-866); unknown for
    // blocks read from a save, which then count as not having fallen.
    std::optional<double> startY;
};
