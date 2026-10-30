// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Protocol messages about the player: movement, health, food, appearance, inventory and crafting.
// Part of the QcMessage variant (QcMessages.h).

#include "common/content/mobs/QcAppearance.h"
#include "common/core/QcBlockPos.h"
#include "common/gameplay/QcAdventureRules.h"
#include "common/gameplay/QcEquipment.h"
#include "common/gameplay/QcGameMode.h"
#include "common/gameplay/QcItemStack.h"
#include "common/gameplay/QcStatusEffects.h"
#include "common/physics/QcBoatMotion.h"
#include "common/physics/QcEntityMotion.h"
#include "common/physics/QcPlayerInput.h"

#include <QList>
#include <QString>

namespace QcMsg {

/*! \brief Client -> server: one tick of movement intent. */
struct PlayerInput
{
    /*! The input of the tick, with its sequence number. */
    QcPlayerInput input;
};

/*! \brief Server -> client: authoritative state of the receiving player.

    \c {vehicle} is the minecart the player rides (entity id, 0 for none, decision E-382): while
    it is set the server moves the player with the cart and the client shows the player where it
    shows the cart, without predicting. It names the boat the player drives as well (decision
    E-920); then \c {boat} is its state after the acknowledged input, which the client's
    prediction of the boat starts from. */
struct PlayerState
{
    /*! The sequence number of the latest input the server applied; the client drops older ones
        from its prediction. */
    quint32 lastInputSequence = 0;
    /*! The player's motion after that input. */
    QcEntityMotion motion;
    /*! The minecart or boat the player is in; 0 for none (see the struct). */
    quint32 vehicle = 0;
    /*! The state of the boat the player drives (see the struct). */
    std::optional<QcBoatState> boat = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free
};

/*! \brief Client -> server: the player selected another hotbar slot. */
struct SelectHotbarSlot
{
    /*! The hotbar slot (0..8). */
    qint32 slot = 0;
};

/*! \brief Client -> server: move \c {count} items from slot \c {from} to slot \c {to}.

    Slots 0..35 are the inventory, 36..44 the crafting grid (row-major, 3 per row; a 2 x 2
    grid uses the top-left four), 45..47 the open machine (input, fuel, output; the output only
    gives), or 45 onwards the slots of the open storage block, or 45 and 47 the open stonecutter
    (input and output, decision E-785), and 100..103 the armour slots (head, chest, legs, feet; only
    armour made for the slot goes in, decision E-234).

    \c {count} 0 moves the whole stack. */
struct MoveInventoryItem
{
    /*! The first slot of the crafting grid. */
    static constexpr qint32 craftingGridSlot = 36;
    /*! The input, fuel and output slots of an open machine. \qcshared */
    static constexpr qint32 machineInputSlot = 45;
    static constexpr qint32 machineFuelSlot = 46;
    static constexpr qint32 machineOutputSlot = 47;
    /*! The first armour slot (head). */
    static constexpr qint32 equipmentSlot = 100;

    /*! Returns whether \a slot is one of the four armour slots. */
    static constexpr bool isEquipmentSlot(qint32 slot) { return slot >= equipmentSlot && slot < equipmentSlot + 4; }

    /*! The slot the items come from. */
    qint32 from = 0;
    /*! The slot they go to. */
    qint32 to = 0;
    /*! How many; 0 moves the whole stack. */
    qint32 count = 0;
};

/*! \brief Client -> server: shift-click on a slot.

    The server moves the whole stack to where it belongs (QcQuickMove): between hotbar and
    storage, from the grid or an open block into the inventory, from the inventory into the open
    chest or the machine's input or fuel, armour from the inventory into its empty armour slot
    and from there back. On the crafting result it crafts as often as the grid and the inventory
    allow. */
struct QuickMoveItem
{
    /*! The slot number of the crafting result. */
    static constexpr qint32 craftingResultSlot = -1;

    /*! Slot numbers as in MoveInventoryItem. */
    qint32 slot = 0;
};

/*! \brief Server -> client: the receiving player's complete inventory, crafting grid and armour
    slots. */
struct InventoryState
{
    /*! The inventory slots (QcInventory::allSlots). */
    QList<QcItemStack> stacks;
    /*! The selected hotbar slot. */
    qint32 selected = 0;
    /*! The nine cells of the crafting grid, row-major. */
    QList<QcItemStack> craftingGrid;
    /*! The grid's size in use: 2 in the inventory, 3 at a crafting table. */
    qint32 craftingSize = 2;
    /*! What the grid crafts now; empty when nothing. */
    QcItemStack craftingResult;
    /*! The armour slots. */
    QcEquipment equipment{};
};

/*! \brief Client -> server: take the crafting result once into the inventory. */
struct TakeCraftingResult
{
};

/*! \brief Client -> server: craft a recipe directly from the inventory (recipe book), \c {count}
    times. */
struct CraftRecipe
{
    /*! The recipe's name. */
    QString recipe;
    /*! How often to craft it; the server takes 1..64. */
    qint32 count = 1;
};

/*! \brief Client -> server: the crafting screen closed; grid items go back to the inventory. */
struct CloseCrafting
{
};

/*! \brief Client -> server: use the held item without a target: eat it, or put on held armour
    (swapping with what is worn in its slot). */
struct ConsumeItem
{
};

/*! \brief Server -> client: open the crafting screen with a grid of this size (crafting table). */
struct OpenCrafting
{
    /*! The grid's size (3 for a crafting table). */
    qint32 size = 3;
};

/*! \brief Server -> client: the receiving player's health; dead players wait for Respawn.

    Also sent when the player catches fire or stops burning (\c {burning}, decision E-393), and
    when his status effects change and every second while he has any (\c {effects}, decision
    E-450). */
struct PlayerHealth
{
    /*! The current and the highest health. \qcshared */
    double health = 0.0;
    double maxHealth = 0.0;
    /*! Whether the player is dead. */
    bool dead = false;
    /*! Whether the player burns. */
    bool burning = false;
    /*! The player's status effects. */
    QList<QcStatusEffect> effects = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free
};

/*! \brief Client -> server: leave the death screen. */
struct Respawn
{
};

/*! \brief Client -> server: the player changed their look. */
struct SetAppearance
{
    /*! The new look. */
    QcAppearance appearance;
};

/*! \brief Client -> server: throw one item (or the whole stack) of the selected hotbar slot. */
struct DropItem
{
    /*! Throws the whole stack instead of one item. */
    bool wholeStack = false;
};

/*! \brief Server -> client: the receiving player's food points. */
struct PlayerFood
{
    /*! The current and the highest food points. \qcshared */
    qint32 food = 0;
    qint32 maxFood = 0;
};

/*! \brief Server -> client: the receiving player's experience (decision E-398): his level and
    the points towards the next one, which needs \c {pointsForNext} (QcExperience::pointsForNext,
    sent so the client needs no curve). */
struct PlayerExperience
{
    /*! The level. */
    qint32 level = 0;
    /*! The points gathered towards the next level. */
    qint32 points = 0;
    /*! The points the next level needs. */
    qint32 pointsForNext = 7;
};

/*! \brief Server -> client: the receiving player's game mode (decision E-282), after the welcome
    and with every change.

    The client derives the rules from it (QcGameModes::rules). In adventure mode it carries the
    world's allow-lists (decision E-735), sent again when /adventure changes them; in other modes
    they are empty. */
struct GameMode
{
    /*! The game mode. */
    QcGameMode mode = QcGameMode::Survival;
    /*! The world's allow-lists in adventure mode; empty in other modes. */
    QcAdventureRules adventure;
};

/*! \brief Client -> server, creative mode only (decision E-283): puts \c {stack} into the inventory
    slot \c {slot} (0..35, or an armour slot 100..103 for armour made for it), onto the same item up
    to its stack size, else in place of what is there; an empty stack clears the slot (the bin).

    \c {addSlot} adds the stack to the inventory like picking it up. The server refuses it in
    other modes. A stack with contents fails to decode; of the other data the server keeps only a
    filled map's id (decision E-823). */
struct CreativeItem
{
    /*! The slot number that adds the stack like picking it up. */
    static constexpr qint32 addSlot = -1;

    /*! The target slot, or addSlot. */
    qint32 slot = addSlot;
    /*! The stack to put there. */
    QcItemStack stack;
};

/*! \brief Client -> server: the middle mouse button on a block (decision E-285).

    The server puts the item of the block into the hand: selects it in the hotbar, swaps it in
    from the storage rows, or in creative mode takes it from the catalogue. */
struct PickBlock
{
    /*! The block clicked. */
    QcBlockPos pos;
};

/*! \brief Client -> server: the use button pressed or released with a launcher in the hand, or the
    draw given up (a screen opened, decision E-425).

    The server times the draw itself; a release shoots when the draw was long enough, a cancel, a
    change of the hotbar slot or the item, or death lets the draw go without a shot. */
struct DrawLauncher
{
    /*! What happened to the draw. */
    enum class Action : quint8 {
        /*! The use button was pressed: the draw starts. */
        Draw,
        /*! The use button was released: shoots when drawn long enough. */
        Release,
        /*! The draw was given up without a shot. */
        Cancel
    };
    /*! What happened. */
    Action action = Action::Release;
};

/*! \brief Client -> server: the use button pressed with a throwable item in the hand (a
    snowball, decision E-427); the server throws it unless its cooldown runs. */
struct ThrowItem
{
};

} // namespace QcMsg
