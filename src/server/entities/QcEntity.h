#pragma once

#include "common/core/QcChunkPos.h"
#include "common/physics/QcEntityMotion.h"
#include "common/world/QcEntityKind.h"
#include "server/entities/QcEntityComponent.h"

#include <QtGlobal>

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
    // Ticks it still burns (players and creatures, QcBodyBurning, decision E-393); not saved.
    int burnTicks = 0;
    // The chunk the last QcEntities::settle saw it in; settle looks up whether a chunk is loaded only
    // when this changed or the loaded chunks did.
    QcChunkPos settledChunk;
    bool settled = false;
    // What its saved entry holds that this version does not use (decision E-420), written back
    // unchanged when it is saved again; empty for nearly every entity.
    QcEntityKept kept;

    // The entity as its type (decision E-372): every type names its kind (`entityKind`), so a
    // mismatch between the kind field and the type asked for is caught in debug builds instead of
    // reading another type's fields.
    template<typename Type>
    Type &as()
    {
        Q_ASSERT(kind == Type::entityKind);
        return static_cast<Type &>(*this);
    }
    template<typename Type>
    const Type &as() const
    {
        Q_ASSERT(kind == Type::entityKind);
        return static_cast<const Type &>(*this);
    }

    bool operator==(const QcEntity &) const = default;
};
