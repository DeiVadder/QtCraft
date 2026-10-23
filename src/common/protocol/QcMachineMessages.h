#pragma once

// Protocol messages of machines (furnaces), storage blocks (chests) and the stonecutter.
// Part of the QcMessage variant (QcMessages.h).

#include "common/core/QcBlockPos.h"
#include "common/gameplay/QcItemStack.h"

#include <QList>
#include <QString>

#include <array>

namespace QcMsg {

// Server -> client: the player opened the machine at `pos`, or with an empty category the storage
// block (chest) there; MachineState follows.
struct OpenMachine
{
    QcBlockPos pos;
    QString category;
    // A container minecart (entity id, decision E-386) instead of the block at `pos`; 0 for a block.
    quint32 entity = 0;
    // A backpack opened from the hand (decision E-833): the hotbar slot that holds it, locked while
    // the screen is open, which shows only the backpack's slots and the hotbar; -1 for a block or cart.
    qint32 heldSlot = noHeldSlot;
    static constexpr qint32 noHeldSlot = -1;
};

// Server -> client: contents and progress of the open machine. `stacks` are input, fuel and output;
// `burn` is the share of the current fuel left and `progress` the share of the current item done.
// For a storage block, `stacks` are its slots and the bars stay at 0.
struct MachineState
{
    static constexpr int slotCount = 3;
    static constexpr int maxSlots = 54;

    QcBlockPos pos;
    // The container minecart of the screen (OpenMachine::entity); 0 for a block.
    quint32 entity = 0;
    QList<QcItemStack> stacks;
    float burn = 0.0f;
    float progress = 0.0f;
};

// Client -> server: the machine screen closed. Server -> client: the machine closed (out of reach,
// broken).
struct CloseMachine
{
};

// Server -> client: the player opened the stonecutter at `pos` (decision E-785); StonecutterState
// follows. Its input and output slots are the slots machineInputSlot and machineOutputSlot of
// MoveInventoryItem and QuickMoveItem while no machine or chest is open.
struct OpenStonecutter
{
    QcBlockPos pos;
};

// Server -> client: the open stonecutter's input slot and the stonecutting recipe chosen for it
// (empty: none). The output follows from both (QcStonecutting::output).
struct StonecutterState
{
    QcItemStack input;
    QString recipe;
};

// Client -> server: choose the stonecutting recipe `recipe` for the input of the open stonecutter
// (empty: none); the server answers with StonecutterState.
struct SelectStonecutterRecipe
{
    QString recipe;
};

// Client -> server: the stonecutter screen closed. Server -> client: the stonecutter closed (broken,
// out of reach).
struct CloseStonecutter
{
};

// Server -> client: the player opened the enchanting table at `pos` (decision E-864); EnchantingState
// follows. Its item and lapis slots are machineInputSlot and machineFuelSlot of MoveInventoryItem
// and QuickMoveItem while no machine or chest is open.
struct OpenEnchanting
{
    QcBlockPos pos;
};

// One of the three offers as the player sees it: the levels it needs (0: no offer) and, as a clue,
// the first enchantment it gives (its content name and level).
struct EnchantingOffer
{
    qint32 levelCost = 0;
    QString clue;
    qint32 clueLevel = 0;

    bool operator==(const EnchantingOffer &) const = default;
};

// Server -> client: the open enchanting table's slots, the bookshelves that count and the offers.
struct EnchantingState
{
    // The longest enchantment name a clue may carry.
    static constexpr qsizetype maxClue = 64;

    QcItemStack item;
    QcItemStack lapis;
    qint32 shelves = 0;
    std::array<EnchantingOffer, 3> offers;
};

// Client -> server: take offer `slot` (0–2) of the open enchanting table.
struct ChooseEnchantment
{
    qint32 slot = 0;
};

// Client -> server: the enchanting screen closed. Server -> client: the table closed (broken, out
// of reach).
struct CloseEnchanting
{
};

// Server -> client: the player opened the anvil at `pos` (decision E-866); AnvilState follows. Its
// left and right slots are machineInputSlot and machineFuelSlot of MoveInventoryItem and
// QuickMoveItem while no machine or chest is open.
struct OpenAnvil
{
    QcBlockPos pos;
};

// Server -> client: the open anvil's slots, the name in its name field, what it would make and the
// levels that costs (QcAnvil::Result; 0 and an empty output for nothing).
struct AnvilState
{
    QcItemStack left;
    QcItemStack right;
    QString name;
    QcItemStack output;
    qint32 levelCost = 0;
};

// Client -> server: the text of the anvil's name field changed (at most QcItemData::maxNameLength
// characters after trimming).
struct RenameAnvil
{
    // The longest text the field sends (spaces around it included); the name is cut to
    // QcItemData::maxNameLength.
    static constexpr qsizetype maxText = 256;

    QString name;
};

// Client -> server: take the anvil's output `output` for `levelCost` levels, as the last
// AnvilState showed them; refused when either changed since.
struct ForgeAnvil
{
    qint32 levelCost = 0;
    QcItemStack output;
};

// Client -> server: the anvil screen closed. Server -> client: the anvil closed (broken, out of reach).
struct CloseAnvil
{
};

} // namespace QcMsg
