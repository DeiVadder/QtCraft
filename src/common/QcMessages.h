#pragma once

// Protocol messages. Plain data structs, grouped here on purpose: together they form the QcMessage
// variant whose alternative index is the wire type tag. New messages are appended at the end.

#include "QcAppearance.h"
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

// Client -> server: first message on every connection.
struct Hello
{
    quint32 protocolVersion = 0;
    QString playerName;
    QcAppearance appearance;
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

// Slots 0..35 are the inventory, 36..44 the crafting grid (row-major, 3 per row; a 2 x 2 grid uses
// the top-left four), 45..47 the open machine (input, fuel, output; the output only gives).
// `count` 0 moves the whole stack.
struct MoveInventoryItem
{
    static constexpr qint32 craftingGridSlot = 36;
    static constexpr qint32 machineInputSlot = 45;
    static constexpr qint32 machineFuelSlot = 46;
    static constexpr qint32 machineOutputSlot = 47;

    qint32 from = 0;
    qint32 to = 0;
    qint32 count = 0;
};

// Client -> server: chat line or command starting with '/'.
struct ChatInput
{
    QString text;
};

// Server -> client: the receiving player's complete inventory and crafting grid.
struct InventoryState
{
    QList<QcItemStack> stacks;
    qint32 selected = 0;
    QList<QcItemStack> craftingGrid;
    qint32 craftingSize = 2;
    QcItemStack craftingResult;
};

// Client -> server: take the crafting result once into the inventory.
struct TakeCraftingResult
{
};

// Client -> server: craft a recipe directly from the inventory (recipe book), `count` times.
struct CraftRecipe
{
    QString recipe;
    qint32 count = 1;
};

// Client -> server: the crafting screen closed; grid items go back to the inventory.
struct CloseCrafting
{
};

// Client -> server: eat the held item.
struct ConsumeItem
{
};

// Server -> client: open the crafting screen with a grid of this size (crafting table).
struct OpenCrafting
{
    qint32 size = 3;
};

// Server -> client: chat line or command feedback.
struct ChatMessage
{
    QString text;
};

// Server -> client: an entity came into view. mobType is an index into the content's mobs, or
// playerType for another player, or itemType for a dropped item stack. Sent again for a known id
// when an item stack's count changes.
struct EntitySpawn
{
    static constexpr qint32 playerType = -1;
    static constexpr qint32 itemType = -2;

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

// Client -> server: the player changed their look.
struct SetAppearance
{
    QcAppearance appearance;
};

// Client -> server: throw one item (or the whole stack) of the selected hotbar slot.
struct DropItem
{
    bool wholeStack = false;
};

// Server -> client: the player opened the machine at `pos`; MachineState follows.
struct OpenMachine
{
    QcBlockPos pos;
    QString category;
};

// Server -> client: contents and progress of the open machine. `stacks` are input, fuel and output;
// `burn` is the share of the current fuel left and `progress` the share of the current item done.
struct MachineState
{
    static constexpr int slotCount = 3;

    QcBlockPos pos;
    QList<QcItemStack> stacks;
    float burn = 0.0f;
    float progress = 0.0f;
};

// Client -> server: the machine screen closed. Server -> client: the machine closed (out of reach,
// broken).
struct CloseMachine
{
};

// Server -> client: the receiving player's food points.
struct PlayerFood
{
    qint32 food = 0;
    qint32 maxFood = 0;
};

// Server -> client: a sound at a world position (block broken, creature hurt, furnace crackling).
// The client picks the loudness and the side from its own distance and view direction.
struct SoundEvent
{
    static constexpr qsizetype maxKeyLength = 256;
    static constexpr float maxVolume = 4.0f;
    static constexpr float minPitch = 0.25f;
    static constexpr float maxPitch = 4.0f;

    QString key;
    QcVec3 position;
    // Loudness relative to the sound's own (louder sounds also carry further) and playback speed.
    float volume = 1.0f;
    float pitch = 1.0f;
};

} // namespace QcMsg

using QcMessage = std::variant<QcMsg::Hello, QcMsg::Welcome, QcMsg::ContentSync, QcMsg::ChunkData, QcMsg::ChunkUnload,
                               QcMsg::BlockUpdate, QcMsg::PlayerInput, QcMsg::PlayerState, QcMsg::Disconnect,
                               QcMsg::StartMining, QcMsg::CancelMining, QcMsg::FinishMining, QcMsg::UseItem,
                               QcMsg::SelectHotbarSlot, QcMsg::MoveInventoryItem, QcMsg::ChatInput,
                               QcMsg::InventoryState, QcMsg::ChatMessage, QcMsg::EntitySpawn, QcMsg::EntityMoves,
                               QcMsg::EntityRemove, QcMsg::PlayerHealth, QcMsg::WorldTime, QcMsg::AttackEntity,
                               QcMsg::Respawn, QcMsg::TakeCraftingResult, QcMsg::CraftRecipe, QcMsg::CloseCrafting,
                               QcMsg::ConsumeItem, QcMsg::OpenCrafting, QcMsg::DropItem, QcMsg::SetAppearance,
                               QcMsg::OpenMachine, QcMsg::MachineState, QcMsg::CloseMachine, QcMsg::PlayerFood,
                               QcMsg::SoundEvent>;
