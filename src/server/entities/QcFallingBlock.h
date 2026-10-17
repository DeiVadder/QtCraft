#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "server/entities/QcEntity.h"

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
};
