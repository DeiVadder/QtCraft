#pragma once

// Protocol messages about the player: movement, health, food, appearance, inventory and crafting.
// Part of the QcMessage variant (QcMessages.h).

#include "QcAppearance.h"
#include "QcBlockPos.h"
#include "QcEntityMotion.h"
#include "QcEquipment.h"
#include "QcGameMode.h"
#include "QcItemStack.h"
#include "QcPlayerInput.h"

#include <QList>
#include <QString>

namespace QcMsg {

// Client -> server: one tick of movement intent.
struct PlayerInput
{
    QcPlayerInput input;
};

// Server -> client: authoritative state of the receiving player. `vehicle` is the minecart the
// player rides (entity id, 0 for none, decision E-382): while it is set the server moves the player
// with the cart and the client shows the player where it shows the cart, without predicting.
struct PlayerState
{
    quint32 lastInputSequence = 0;
    QcEntityMotion motion;
    quint32 vehicle = 0;
};

struct SelectHotbarSlot
{
    qint32 slot = 0;
};

// Slots 0..35 are the inventory, 36..44 the crafting grid (row-major, 3 per row; a 2 x 2 grid uses
// the top-left four), 45..47 the open machine (input, fuel, output; the output only gives), or 45
// onwards the slots of the open storage block, and 100..103 the armour slots (head, chest, legs,
// feet; only armour made for the slot goes in, decision E-234).
// `count` 0 moves the whole stack.
struct MoveInventoryItem
{
    static constexpr qint32 craftingGridSlot = 36;
    static constexpr qint32 machineInputSlot = 45;
    static constexpr qint32 machineFuelSlot = 46;
    static constexpr qint32 machineOutputSlot = 47;
    static constexpr qint32 equipmentSlot = 100;

    static constexpr bool isEquipmentSlot(qint32 slot) { return slot >= equipmentSlot && slot < equipmentSlot + 4; }

    qint32 from = 0;
    qint32 to = 0;
    qint32 count = 0;
};

// Client -> server: shift-click on a slot. The server moves the whole stack to where it belongs
// (QcQuickMove): between hotbar and storage, from the grid or an open block into the inventory,
// from the inventory into the open chest or the machine's input or fuel, armour from the inventory
// into its empty armour slot and from there back. On the crafting result it crafts as often as the
// grid and the inventory allow.
struct QuickMoveItem
{
    static constexpr qint32 craftingResultSlot = -1;

    // Slot numbers as in MoveInventoryItem.
    qint32 slot = 0;
};

// Server -> client: the receiving player's complete inventory, crafting grid and armour slots.
struct InventoryState
{
    QList<QcItemStack> stacks;
    qint32 selected = 0;
    QList<QcItemStack> craftingGrid;
    qint32 craftingSize = 2;
    QcItemStack craftingResult;
    QcEquipment equipment{};
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

// Client -> server: use the held item without a target: eat it, or put on held armour (swapping
// with what is worn in its slot).
struct ConsumeItem
{
};

// Server -> client: open the crafting screen with a grid of this size (crafting table).
struct OpenCrafting
{
    qint32 size = 3;
};

// Server -> client: the receiving player's health; dead players wait for Respawn.
struct PlayerHealth
{
    double health = 0.0;
    double maxHealth = 0.0;
    bool dead = false;
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

// Server -> client: the receiving player's food points.
struct PlayerFood
{
    qint32 food = 0;
    qint32 maxFood = 0;
};

// Server -> client: the receiving player's game mode (decision E-282), after the welcome and with
// every change. The client derives the rules from it (QcGameModes::rules).
struct GameMode
{
    QcGameMode mode = QcGameMode::Survival;
};

// Client -> server, creative mode only (decision E-283): puts `stack` into the inventory slot `slot`
// (0..35, or an armour slot 100..103 for armour made for it), onto the same item up to its stack
// size, else in place of what is there; an empty stack clears the slot (the bin). `addSlot` adds
// the stack to the inventory like picking it up. The server refuses it in other modes.
struct CreativeItem
{
    static constexpr qint32 addSlot = -1;

    qint32 slot = addSlot;
    QcItemStack stack;
};

// Client -> server: the middle mouse button on a block (decision E-285). The server puts the item of
// the block into the hand: selects it in the hotbar, swaps it in from the storage rows, or in
// creative mode takes it from the catalogue.
struct PickBlock
{
    QcBlockPos pos;
};

} // namespace QcMsg
