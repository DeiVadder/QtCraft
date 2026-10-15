#pragma once

// Protocol messages about entities: creatures, other players, dropped items, falling and sliding
// blocks, armour stands and minecarts.
// Part of the QcMessage variant (QcMessages.h).

#include "QcAppearance.h"
#include "QcEquipment.h"
#include "QcBlockDefinition.h"
#include "QcEntityKind.h"
#include "QcItemStack.h"
#include "QcStandPose.h"
#include "QcVec3.h"

#include <QList>
#include <QString>

namespace QcMsg {

// Movement and action states of an entity, as bits of EntitySpawn/EntityMove::flags. The client
// animates from them.
enum EntityFlag : quint16 {
    // A swing of the right arm started this tick (mining, attacking, using).
    Swing = 1,
    OnGround = 2,
    InWater = 4,
    Sneaking = 8,
    Sprinting = 16,
    // A young animal (decision E-293): drawn at QcMsg::youngScale of its size.
    Young = 32,
    // An animal in love after feeding: hearts above it.
    InLove = 64,
    // A player sitting in a minecart (decision E-382).
    Riding = 128,
    // On fire (decision E-393): flames around the body.
    Burning = 256,
};

inline constexpr double youngScale = 0.5;

// Server -> client: an entity came into view, with its kind (decision E-365); a creature also names
// its type, an index into the content's mobs. Sent again for a known id when an item stack's count,
// a player's look or armour or a stand changes.
struct EntitySpawn
{
    quint32 id = 0;
    QcEntityKind kind = QcEntityKind::Player;
    QString name;
    QcVec3 position;
    float yaw = 0.0f;
    double health = 0.0;
    // The stack of an item entity; empty for the other kinds.
    QcItemStack item;
    // Players: their chosen look; armour stands: only its cape counts (decision E-239).
    QcAppearance appearance;
    // View pitch in degrees, EntityFlag bits and the item in the right hand.
    float pitch = 0.0f;
    quint16 flags = 0;
    QcItemId held = qcNoItem;
    // The block of a falling or moving block entity; air otherwise.
    QcBlockId block = qcAirBlockId;
    // Players and armour stands: what they wear, so others draw it (decision E-234); empty for the rest.
    QcEquipment equipment{};
    // Armour stands only: pose and what they show.
    QcStandPose stand{};
    // Creatures only: index into the content's mobs (at most QcStreamLimits::maxDefinitions).
    qint32 creatureType = 0;
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
    quint16 flags = 0;
    QcItemId held = qcNoItem;
};

// Server -> client: positions of all visible entities that changed this tick, with the server's
// time of the tick (players' time, QcServerClock::playerTimeNs, in ms) for the client's
// interpolation buffer (decision E-271).
struct EntityMoves
{
    QList<EntityMove> moves;
    qint64 timeMs = 0;
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

// Client -> server: use the held item on a creature (catch a firefly with an empty bottle) or on
// an armour stand (put on or take off armour or the held item; the server picks the body part from
// the player's view).
struct UseEntity
{
    quint32 id = 0;
};

} // namespace QcMsg
