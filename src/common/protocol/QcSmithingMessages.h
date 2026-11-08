// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Protocol messages of the smithing table (decision E-1115). Part of the QcMessage variant
// (QcMessages.h).

#include "common/core/QcBlockPos.h"
#include "common/gameplay/QcItemStack.h"

namespace QcMsg {

/*! \brief Server -> client: the player opened the smithing table at \c {pos} (decision E-1115);
    SmithingState follows.

    Its template, base and addition slots are machineInputSlot, machineFuelSlot and
    machineOutputSlot of MoveInventoryItem and QuickMoveItem while no machine or chest is open. */
struct OpenSmithing
{
    /*! The smithing table's block. */
    QcBlockPos pos;
};

/*! \brief Server -> client: the open smithing table's slots and what they make
    (QcSmithing::output; empty for nothing). \qcfields */
struct SmithingState
{
    QcItemStack templateStack;
    QcItemStack base;
    QcItemStack addition;
    QcItemStack output;
};

/*! \brief Client -> server: take the output \c {output} the last SmithingState showed; refused
    when it changed since. */
struct ForgeSmithing
{
    /*! The output the last SmithingState showed. */
    QcItemStack output;
};

/*! \brief Client -> server: the smithing screen closed.

    Server -> client: the smithing table closed (broken, out of reach). */
struct CloseSmithing
{
};

} // namespace QcMsg
