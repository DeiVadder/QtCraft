// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

/*! \brief An anvil swap (decision E-870): the anvil turns a stack of one item into the same number
    of \c {into}, using up one \c {with} from its right slot.

    A block declares it on one side (block field \c {anvilSwap}); QcAnvilSwaps gives both items
    the swap, each pointing at the other. Names are qualified. Server side only. */
struct QcAnvilSwap
{
    /*! Qualified name of the item the stack turns into. */
    QString into;
    /*! Qualified name of the item used up from the anvil's right slot, one per swap. */
    QString with;

    bool operator==(const QcAnvilSwap &) const = default;
};
