#pragma once

// Protocol messages about entities: mobs, other players, dropped items and falling blocks.
// Part of the QcMessage variant (QcMessages.h).

#include "QcAppearance.h"
#include "QcBlockDefinition.h"
#include "QcItemStack.h"
#include "QcVec3.h"

#include <QList>
#include <QString>

namespace QcMsg {

// Movement and action states of an entity, as bits of EntitySpawn/EntityMove::flags. The client
// animates from them.
enum EntityFlag : quint8 {
    // A swing of the right arm started this tick (mining, attacking, using).
    Swing = 1,
    OnGround = 2,
    InWater = 4,
    Sneaking = 8,
    Sprinting = 16,
};

// Server -> client: an entity came into view. mobType is an index into the content's mobs, or
// playerType for another player, or itemType for a dropped item stack. Sent again for a known id
// when an item stack's count changes.
struct EntitySpawn
{
    static constexpr qint32 playerType = -1;
    static constexpr qint32 itemType = -2;
    // A falling block (sand, gravel) in the state `block`.
    static constexpr qint32 fallingBlockType = -3;

    quint32 id = 0;
    qint32 mobType = playerType;
    QString name;
    QcVec3 position;
    float yaw = 0.0f;
    double health = 0.0;
    // The stack of an item entity; empty for mobs and players.
    QcItemStack item;
    // Players only: their chosen look.
    QcAppearance appearance;
    // View pitch in degrees, EntityFlag bits and the item in the right hand.
    float pitch = 0.0f;
    quint8 flags = 0;
    QcItemId held = qcNoItem;
    // The block of a falling block entity; air otherwise.
    QcBlockId block = qcAirBlockId;
};

struct EntityMove
{
    quint32 id = 0;
    QcVec3 position;
    float yaw = 0.0f;
    double health = 0.0;
    // Set in the tick the entity took damage (for a hurt flash).
    bool hurt = false;
    float pitch = 0.0f;
    quint8 flags = 0;
    QcItemId held = qcNoItem;
};

// Server -> client: positions of all visible entities that changed this tick.
struct EntityMoves
{
    QList<EntityMove> moves;
};

struct EntityRemove
{
    quint32 id = 0;
    // The entity died (the client plays the death animation) rather than left the view.
    bool died = false;
};

// Client -> server: hit an entity with the held item.
struct AttackEntity
{
    quint32 id = 0;
};

// Client -> server: use the held item on a creature (catch a firefly with an empty bottle).
struct UseEntity
{
    quint32 id = 0;
};

} // namespace QcMsg
