#pragma once

#include "QcBlockDefinition.h"
#include "QcBlockPos.h"
#include "QcEntity.h"

// A block sliding where a piston moved it, as the clients see it (decisions E-337, E-364): the
// world holds the block at `to` already; the entity only shows the slide (QcMovingBlocks).
struct QcMovingBlock : QcEntity
{
    QcMovingBlock()
        : QcEntity(QcEntityKind::MovingBlock)
    {
    }

    QcBlockId block = qcAirBlockId;
    QcBlockPos from;
    QcBlockPos to;
    quint64 start = 0;
};
