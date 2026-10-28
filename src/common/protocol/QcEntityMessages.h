#pragma once

// Protocol messages about entities: creatures, other players, dropped items, falling and sliding
// blocks, armour stands and minecarts.
// Part of the QcMessage variant (QcMessages.h).

#include "common/content/definitions/QcAppearance.h"
#include "common/content/definitions/QcBlockDefinition.h"
#include "common/core/QcVec3.h"
#include "common/gameplay/QcEquipment.h"
#include "common/gameplay/QcHanging.h"
#include "common/gameplay/QcItemStack.h"
#include "common/gameplay/QcStandPose.h"
#include "common/physics/QcPose.h"
#include "common/world/QcEntityKind.h"

#include <QList>
#include <QString>

namespace QcMsg {

// Movement and action states of an entity, as bits of EntitySpawn/EntityMove::flags. The client
// animates from them.
// A swing of the right arm started this tick (mining, attacking, using).
inline constexpr quint16 Swing = 1;
inline constexpr quint16 OnGround = 2;
inline constexpr quint16 InWater = 4;
inline constexpr quint16 Sneaking = 8;
inline constexpr quint16 Sprinting = 16;
// On a boat the sprint bit tells that its driver paddles: his latest input pressed a key that
// paddles or turns (decision E-920); the paddles row.
inline constexpr quint16 Rowing = Sprinting;
// A young animal (decision E-293): drawn at QcMsg::youngScale of its size.
inline constexpr quint16 Young = 32;
// An animal in love after feeding: hearts above it.
inline constexpr quint16 InLove = 64;
// A player sitting in a minecart (decision E-382).
inline constexpr quint16 Riding = 128;
// On fire (decision E-393): flames around the body.
inline constexpr quint16 Burning = 256;
// The pose of a creature (QcPose, decision E-423): lying on its back (a villager in its bed,
// decision E-411) or sitting; neither for standing.
inline constexpr quint16 Lying = 512;
inline constexpr quint16 Sitting = 1024;
// A player drawing a bow (decision E-425): arms raised to aim in the third person.
inline constexpr quint16 Drawing = 2048;
// A projectile stuck in a block: it no longer turns with its flight.
inline constexpr quint16 Stuck = 4096;

// The pose bits of a creature's flags, and the pose they carry.
inline quint16 poseFlags(QcPose pose)
{
    return pose == QcPose::Lying ? static_cast<quint16>(Lying) : pose == QcPose::Sitting ? static_cast<quint16>(Sitting) : quint16{0};
}
inline QcPose poseOf(quint16 flags)
{
    return (flags & Lying) != 0 ? QcPose::Lying : (flags & Sitting) != 0 ? QcPose::Sitting : QcPose::Standing;
}

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
    // Players and creatures: their health; experience orbs: their value (decision E-398).
    double health = 0.0;
    // The stack of an item entity; projectiles: the item they look like (an arrow); empty for the other kinds.
    QcItemStack item;
    // Players: their chosen look; villagers: the look the server gives them (decision E-411); armour
    // stands: only its cape counts (decision E-239).
    QcAppearance appearance;
    // View pitch in degrees, entity flag bits (QcMsg::Swing …) and the item in the right hand.
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
    // Item frames and paintings only (decision E-790): where they hang and what they show; `held`
    // names their own item (the client finds a painting's motifs there), `item` is the stack a
    // frame holds.
    QcHanging hanging{};
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
