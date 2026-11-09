// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/gameplay/QcEnchantingTable.h"
#include "common/gameplay/QcInventory.h"
#include "common/gameplay/QcSmithing.h"
#include "common/protocol/QcMessages.h"

#include <QList>
#include <QString>

#include <optional>

// The screens a player has open on the server (QcServerClient::screen) and what each holds.

/*! \brief The open stonecutter (QcServerStonecutter, decision E-785): where it stands, the
    stack in its input slot and the stonecutting recipe chosen for it (empty: none).

    Its output is not stored; it follows from both (QcStonecutting::output). \qcfields */
struct QcOpenStonecutter
{
    QcBlockPos pos;
    QcItemStack input;
    QString recipe;

    /*! Puts \a stack into the input slot; another item or none drops the chosen recipe, as
        in the reference (more or fewer of the same item keep it). */
    void setInput(const QcItemStack &stack)
    {
        if (stack.isEmpty() || stack.item != input.item) {
            recipe.clear();
        }
        input = stack.isEmpty() ? QcItemStack() : stack;
    }
};

/*! \brief The open enchanting table (QcServerEnchanting, decision E-864): where it stands and
    the stacks in its item and lapis slots.

    Its offers follow from the item, the bookshelves and the player's enchantment seed.
    \c {shown} and \c {shownShelves} are what the client was last told (stateOf): the player
    takes only an offer he saw, and a changed shelf count sends the offers again. \qcfields */
struct QcOpenEnchanting
{
    QcBlockPos pos;
    QcItemStack item;
    QcItemStack lapis;
    QcEnchantingTable::Offers shown{};
    int shownShelves = -1;
};

/*! \brief The open anvil (QcServerAnvil, decision E-866): where it stands, its left and right
    slots and the text of its name field.

    What it makes follows from them (QcAnvil::result). The name field follows the left stack:
    whenever another stack (or the same with other data) lies there, the field holds that
    stack's custom name, so a repair keeps the name (as in the reference). \qcfields */
struct QcOpenAnvil
{
    QcBlockPos pos;
    QcItemStack left;
    QcItemStack right;
    QString name;

    /*! Puts \a stack into the left slot; another item or other data takes its custom name
        into the name field. */
    void setLeft(const QcItemStack &stack)
    {
        if (stack.item != left.item || !(stack.itemData() == left.itemData())) {
            name = stack.name();
        }
        left = stack;
    }
};

/*! \brief The open smithing table (QcServerSmithing, decision E-1115): where it stands and the
    stacks in its template, base and addition slots (by QcSmithing::Slot).

    What it makes follows from them (QcSmithing::output). \qcfields */
struct QcOpenSmithing
{
    QcBlockPos pos;
    QcSmithing::Inputs inputs;
};

/*! \brief The open beacon (QcServerBeacons, decision E-1142): where it stands and the stack in
    its payment slot. \qcfields */
struct QcOpenBeacon
{
    QcBlockPos pos;
    QcItemStack payment;
};

/*! \brief The machine, container or villager whose screen the player has open, and what the
    client was last told about it.

    A portable container opened from the hand (a backpack, decision E-833): the hotbar slot
    that holds it, locked while open, and its slots as a working copy of the item's contents,
    filled from the item whenever the screen is used (QcBlockScreens::openContainerOf) and
    written back into it by QcServerInventory. \qcfields */
struct QcHeldContainer
{
    int slot = 0;
    QList<QcItemStack> stacks;
};

/*! \brief The screens the player has open and what the client was last told about them. */
struct QcPlayerScreen
{
    /*! The machine or container block whose screen is open. */
    std::optional<QcBlockPos> open;
    /*! A container minecart (entity id) instead of a block; \c {open} is unset then. */
    quint32 cart = 0;
    /*! A backpack opened from the hand instead of a block or cart; \c {open} is unset and
        \c {cart} 0 then. */
    std::optional<QcHeldContainer> held;
    /*! The machine screen as last sent. */
    std::optional<QcMsg::MachineState> sent;
    /*! The villager (entity id) whose trading screen is open (QcServerTrading), 0 for none,
        and the screen as last sent. \qcshared */
    quint32 trading = 0;
    std::optional<QcMsg::TradeState> sentTrade;
    /*! The stonecutter whose screen is open. It, a machine, chest or cart (\c {open},
        \c {cart}) and a trade (\c {trading}) are never open at once: opening one closes the
        others (QcServerStonecutter::open, QcBlockScreens::open/openCart,
        QcServerTrading::open). */
    std::optional<QcOpenStonecutter> stonecutter;
    /*! The enchanting table whose screen is open; never together with any of the above. */
    std::optional<QcOpenEnchanting> enchanting;
    /*! The anvil whose screen is open; never together with any of the above. */
    std::optional<QcOpenAnvil> anvil;
    /*! The smithing table whose screen is open; never together with any of the above. */
    std::optional<QcOpenSmithing> smithing;
    /*! The beacon whose screen is open; never together with any of the above. */
    std::optional<QcOpenBeacon> beacon;
    /*! The sign whose editor the server opened for the player (QcServerSigns, decision
        E-766). */
    std::optional<QcBlockPos> sign;

    /*! Returns the stacks in the slots of the open workstation screen (stonecutter, enchanting
        table, anvil, smithing table or beacon), empty ones included; none without such a
        screen. */
    QList<QcItemStack> workstationStacks() const
    {
        QList<QcItemStack> stacks;
        if (stonecutter) {
            stacks.append(stonecutter->input);
        }
        if (enchanting) {
            stacks.append({enchanting->item, enchanting->lapis});
        }
        if (anvil) {
            stacks.append({anvil->left, anvil->right});
        }
        if (smithing) {
            stacks.append(QList<QcItemStack>(smithing->inputs.begin(), smithing->inputs.end()));
        }
        if (beacon) {
            stacks.append(beacon->payment);
        }
        return stacks;
    }

    /*! Returns workstationStacks() and empties those slots: they go back to the player, onto
        the ground or into the player's file. */
    QList<QcItemStack> takeWorkstationStacks()
    {
        QList<QcItemStack> stacks = workstationStacks();
        if (stonecutter && !stonecutter->input.isEmpty()) {
            stonecutter->setInput({});
        }
        if (enchanting) {
            enchanting->item = {};
            enchanting->lapis = {};
        }
        if (anvil) {
            anvil->left = {};
            anvil->right = {};
        }
        if (smithing) {
            smithing->inputs = {};
        }
        if (beacon) {
            beacon->payment = {};
        }
        return stacks;
    }
};
