#pragma once

#include "QcEntityKind.h"
#include "QcEntityMotion.h"

// What every entity of the server has (decision E-364): an id unique among all entities of the
// world, its kind, where it is and how it moves (feet in the middle of the bottom face), the
// direction it faces and whether it rests because its chunk (QcEntities::chunkOf) is not loaded. The kinds add their own
// state (QcCreature, QcItemEntity, QcFallingBlock, QcArmourStand, QcMovingBlock, QcServerClient for
// players); QcEntities keeps them all.
struct QcEntity
{
    explicit QcEntity(QcEntityKind entityKind)
        : kind(entityKind)
    {
    }

    quint32 id = 0;
    QcEntityKind kind;
    QcEntityMotion motion;
    // Degrees, as the protocol sends it.
    float yaw = 0.0f;
    // Its chunk was not loaded at the start of the tick (QcEntities::settle): it is not simulated,
    // not sent to clients and not counted. Players never rest.
    bool dormant = false;

    bool operator==(const QcEntity &) const = default;
};
