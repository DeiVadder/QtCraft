#pragma once

#include "QcBlockDefinition.h"
#include "QcEntity.h"

// A block falling while nothing holds it (decision E-055, E-364); QcFallingBlockManager lets it fall
// and land.
struct QcFallingBlock : QcEntity
{
    QcFallingBlock()
        : QcEntity(QcEntityKind::FallingBlock)
    {
    }

    QcBlockId block = qcAirBlockId;
    double age = 0.0;
};
