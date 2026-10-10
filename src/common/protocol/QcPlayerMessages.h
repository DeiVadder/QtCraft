#pragma once

// Protocol messages about the player: movement, health, food, appearance, inventory and crafting.
// Part of the QcMessage variant (QcMessages.h).

#include "QcAppearance.h"
#include "QcEntityMotion.h"
#include "QcEquipment.h"
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

// Server -> client: authoritative state of the receiving player.
struct PlayerState
{
    quint32 lastInputSequence = 0;
    QcEntityMotion motion;
};

struct SelectHotbarSlot
{
    qint32 slot = 0;
};

// Slots 0..35 are the inventory, 36..44 the crafting grid (row-major, 3 per row; a 2 x 2 grid uses
// the top-left four), 45..47 the open machine (input, fuel, output; the output only gives), or 45
// onwards the slots of the open storage block.
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

// Client -> server: shift-click on a slot. The server moves the whole stack to where it belongs
// (QcQuickMove): between hotbar and storage, from the grid or an open block into the inventory,
// from the inventory into the open chest or the machine's input or fuel. On the crafting result it
// crafts as often as the grid and the inventory allow.
struct QuickMoveItem
{
    static constexpr qint32 craftingResultSlot = -1;

    // Slot numbers as in MoveInventoryItem.
    qint32 slot = 0;
};

// Server -> client: the receiving player's complete inventory, crafting grid and armour slots
// (empty until stage 23 adds armour).
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

// Client -> server: eat the held item.
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

} // namespace QcMsg
