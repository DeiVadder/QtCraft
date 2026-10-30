// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Protocol messages of machines (furnaces), storage blocks (chests) and the stonecutter.
// Part of the QcMessage variant (QcMessages.h).

#include "common/core/QcBlockPos.h"
#include "common/gameplay/QcItemStack.h"

#include <QList>
#include <QString>

#include <array>

/* QcMsg is documented in QcSessionMessages.h. */
namespace QcMsg {

/*! \brief Server -> client: the player opened the machine at \c {pos}, or with an empty category
    the storage block (chest) there; MachineState follows. */
struct OpenMachine
{
    /*! The block of the machine or chest. */
    QcBlockPos pos;
    /*! The machine's category (QcMachineDefinition::category); empty for a storage block. */
    QString category;
    /*! A container minecart (entity id, decision E-386) instead of the block at \c {pos}; 0 for
        a block. */
    quint32 entity = 0;
    /*! A backpack opened from the hand (decision E-833): the hotbar slot that holds it, locked
        while the screen is open, which shows only the backpack's slots and the hotbar; -1 for a
        block or cart. */
    qint32 heldSlot = noHeldSlot;
    /*! The \c {heldSlot} of a block or cart. */
    static constexpr qint32 noHeldSlot = -1;
    /*! The custom name of the chest or machine (decision E-873), the screen's title; empty: the
        block's own name. */
    QString name = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisation warning-free
};

/*! \brief Server -> client: contents and progress of the open machine.

    \c {stacks} are input, fuel and output; \c {burn} is the share of the current fuel left and
    \c {progress} the share of the current item done. For a storage block, \c {stacks} are its
    slots and the bars stay at 0. */
struct MachineState
{
    /*! The slots of a machine: input, fuel and output. */
    static constexpr int slotCount = 3;
    /*! The most slots; the decoder refuses more. */
    static constexpr int maxSlots = 54;

    /*! The block of the machine or chest. */
    QcBlockPos pos;
    /*! The container minecart of the screen (OpenMachine::entity); 0 for a block. */
    quint32 entity = 0;
    /*! The slots (see the struct). */
    QList<QcItemStack> stacks;
    /*! The share of the current fuel left (see the struct). */
    float burn = 0.0f;
    /*! The share of the current item done (see the struct). */
    float progress = 0.0f;
};

/*! \brief Client -> server: the machine screen closed.

    Server -> client: the machine closed (out of reach, broken). */
struct CloseMachine
{
};

/*! \brief Server -> client: the player opened the stonecutter at \c {pos} (decision E-785);
    StonecutterState follows.

    Its input and output slots are the slots machineInputSlot and machineOutputSlot of
    MoveInventoryItem and QuickMoveItem while no machine or chest is open. */
struct OpenStonecutter
{
    /*! The stonecutter's block. */
    QcBlockPos pos;
};

/*! \brief Server -> client: the open stonecutter's input slot and the stonecutting recipe chosen
    for it (empty: none).

    The output follows from both (QcStonecutting::output). */
struct StonecutterState
{
    /*! The input slot. */
    QcItemStack input;
    /*! The chosen recipe's name; empty for none. */
    QString recipe;
};

/*! \brief Client -> server: choose the stonecutting recipe \c {recipe} for the input of the open
    stonecutter (empty: none); the server answers with StonecutterState. */
struct SelectStonecutterRecipe
{
    /*! The recipe's name; empty for none. */
    QString recipe;
};

/*! \brief Client -> server: the stonecutter screen closed.

    Server -> client: the stonecutter closed (broken, out of reach). */
struct CloseStonecutter
{
};

/*! \brief Server -> client: the player opened the enchanting table at \c {pos} (decision E-864);
    EnchantingState follows.

    Its item and lapis slots are machineInputSlot and machineFuelSlot of MoveInventoryItem and
    QuickMoveItem while no machine or chest is open. */
struct OpenEnchanting
{
    /*! The table's block. */
    QcBlockPos pos;
};

/*! \brief One of the three offers as the player sees it: the levels it needs (0: no offer) and,
    as a clue, the first enchantment it gives (its content name and level). */
struct EnchantingOffer
{
    /*! The levels the offer needs; 0 for no offer. */
    qint32 levelCost = 0;
    /*! The content name of the first enchantment it gives. */
    QString clue;
    /*! The level of that enchantment. */
    qint32 clueLevel = 0;

    bool operator==(const EnchantingOffer &) const = default;
};

/*! \brief Server -> client: the open enchanting table's slots, the bookshelves that count, the
    offers and the rune seed the screen writes the clues with (decision E-884: a random value of
    the player's own, drawn anew with the enchanting seed but not derived from it, so the order
    of the readable letters stays until the player enchants and tells nothing about the offers). */
struct EnchantingState
{
    /*! The longest enchantment name a clue may carry. */
    static constexpr qsizetype maxClue = 64;

    /*! The item slot. */
    QcItemStack item;
    /*! The lapis slot. */
    QcItemStack lapis;
    /*! The bookshelves that count. */
    qint32 shelves = 0;
    /*! The three offers. */
    std::array<EnchantingOffer, 3> offers;
    /*! The rune seed (see the struct). */
    quint32 runeSeed = 0;
};

/*! \brief Client -> server: take offer \c {slot} (0–2) of the open enchanting table. */
struct ChooseEnchantment
{
    /*! The offer (0–2). */
    qint32 slot = 0;
};

/*! \brief Client -> server: the enchanting screen closed.

    Server -> client: the table closed (broken, out of reach). */
struct CloseEnchanting
{
};

/*! \brief Server -> client: the player opened the anvil at \c {pos} (decision E-866); AnvilState
    follows.

    Its left and right slots are machineInputSlot and machineFuelSlot of MoveInventoryItem and
    QuickMoveItem while no machine or chest is open. */
struct OpenAnvil
{
    /*! The anvil's block. */
    QcBlockPos pos;
};

/*! \brief Server -> client: the open anvil's slots, the name in its name field, what it would
    make and the levels that costs (QcAnvil::Result; 0 and an empty output for nothing). */
struct AnvilState
{
    /*! The left slot. */
    QcItemStack left;
    /*! The right slot. */
    QcItemStack right;
    /*! The text in the name field. */
    QString name;
    /*! What the anvil would make; empty for nothing. */
    QcItemStack output;
    /*! The levels that costs; 0 for nothing. */
    qint32 levelCost = 0;
};

/*! \brief Client -> server: the text of the anvil's name field changed (at most
    QcItemData::maxNameLength characters after trimming). */
struct RenameAnvil
{
    /*! The longest text the field sends (spaces around it included); the name is cut to
        QcItemData::maxNameLength. */
    static constexpr qsizetype maxText = 256;

    /*! The field's text. */
    QString name;
};

/*! \brief Client -> server: take the anvil's output \c {output} for \c {levelCost} levels, as
    the last AnvilState showed them; refused when either changed since. */
struct ForgeAnvil
{
    /*! The levels the last AnvilState showed. */
    qint32 levelCost = 0;
    /*! The output the last AnvilState showed. */
    QcItemStack output;
};

/*! \brief Client -> server: the anvil screen closed.

    Server -> client: the anvil closed (broken, out of reach). */
struct CloseAnvil
{
};

} // namespace QcMsg
