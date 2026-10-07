#pragma once

// Protocol messages. Plain data structs, grouped here on purpose: together they form the QcMessage
// variant whose alternative index is the wire type tag. New messages are appended at the end.

#include "QcBlockDefinition.h"
#include "QcBlockPos.h"
#include "QcChunk.h"
#include "QcContentRegistry.h"
#include "QcEntityMotion.h"
#include "QcItemStack.h"
#include "QcPlayerInput.h"

#include <QString>

#include <memory>
#include <variant>

namespace QcMsg {

// Client -> server: first message on every connection.
struct Hello
{
    quint32 protocolVersion = 0;
    QString playerName;
};

// Server -> client: accepted connection.
struct Welcome
{
    quint32 protocolVersion = 0;
    quint32 playerEntityId = 0;
    QString worldName;
    qint64 seed = 0;
    // View direction from the save; only meaningful when `restoredView` is set.
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool restoredView = false;
};

// Server -> client: all content definitions; runtime block ids are the indices in this registry.
struct ContentSync
{
    std::shared_ptr<const QcContentRegistry> content;
};

struct ChunkData
{
    std::shared_ptr<const QcChunk> chunk;
};

struct ChunkUnload
{
    QcChunkPos pos;
};

struct BlockUpdate
{
    QcBlockPos pos;
    QcBlockId block = qcAirBlockId;
};

// Client -> server: one tick of movement intent.
struct PlayerInput
{
    QcPlayerInput input;
};

// Server -> client: authoritative state of the receiving player.
struct PlayerState
{
    quint32 lastInputSequence = 0;
    QcEntityMotion motion;
};

struct Disconnect
{
    QString reason;
};

// Client -> server: the player starts hitting a block.
struct StartMining
{
    QcBlockPos pos;
};

// Client -> server: the player stopped hitting before the block broke.
struct CancelMining
{
};

// Client -> server: the client's timer says the block is broken; the server checks the time itself.
struct FinishMining
{
    QcBlockPos pos;
};

// Client -> server: use the held item on a block face (places blocks).
struct UseItem
{
    QcBlockPos target;
    QcBlockFace face = QcBlockFace::Up;
};

struct SelectHotbarSlot
{
    qint32 slot = 0;
};

struct MoveInventoryItem
{
    qint32 from = 0;
    qint32 to = 0;
};

// Client -> server: chat line or command starting with '/'.
struct ChatInput
{
    QString text;
};

// Server -> client: the receiving player's complete inventory.
struct InventoryState
{
    QList<QcItemStack> stacks;
    qint32 selected = 0;
};

// Server -> client: chat line or command feedback.
struct ChatMessage
{
    QString text;
};

// Server -> client: an entity came into view. mobType is an index into the content's mobs, or -1
// for another player.
struct EntitySpawn
{
    quint32 id = 0;
    qint32 mobType = -1;
    QString name;
    QcVec3 position;
    float yaw = 0.0f;
    double health = 0.0;
};

struct EntityMove
{
    quint32 id = 0;
    QcVec3 position;
    float yaw = 0.0f;
    double health = 0.0;
    // Set in the tick the entity took damage (for a hurt flash).
    bool hurt = false;
};

// Server -> client: positions of all visible entities that changed this tick.
struct EntityMoves
{
    QList<EntityMove> moves;
};

struct EntityRemove
{
    quint32 id = 0;
};

// Server -> client: the receiving player's health; dead players wait for Respawn.
struct PlayerHealth
{
    double health = 0.0;
    double maxHealth = 0.0;
    bool dead = false;
};

// Server -> client: seconds since the world began and the length of a full day.
struct WorldTime
{
    double time = 0.0;
    double dayLength = 0.0;
};

// Client -> server: hit an entity with the held item.
struct AttackEntity
{
    quint32 id = 0;
};

// Client -> server: leave the death screen.
struct Respawn
{
};

} // namespace QcMsg

using QcMessage = std::variant<QcMsg::Hello, QcMsg::Welcome, QcMsg::ContentSync, QcMsg::ChunkData, QcMsg::ChunkUnload,
                               QcMsg::BlockUpdate, QcMsg::PlayerInput, QcMsg::PlayerState, QcMsg::Disconnect,
                               QcMsg::StartMining, QcMsg::CancelMining, QcMsg::FinishMining, QcMsg::UseItem,
                               QcMsg::SelectHotbarSlot, QcMsg::MoveInventoryItem, QcMsg::ChatInput,
                               QcMsg::InventoryState, QcMsg::ChatMessage, QcMsg::EntitySpawn, QcMsg::EntityMoves,
                               QcMsg::EntityRemove, QcMsg::PlayerHealth, QcMsg::WorldTime, QcMsg::AttackEntity,
                               QcMsg::Respawn>;
