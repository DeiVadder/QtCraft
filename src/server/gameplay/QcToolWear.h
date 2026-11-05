// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcItemStack.h"

class QcServerState;

/*! \brief One use of an item that wears (decision E-864): a tool mining, a weapon hitting, a bow
    shooting.

    Unbreaking lets a use leave the durability with the chance of QcEnchanting::keepsDurability,
    drawn from the server's random generator. */
namespace QcToolWear {

/*! Returns \a tool after one use: one more damage, empty when that breaks it; unchanged for
    items without durability or when unbreaking keeps it (dice from \a state). */
[[nodiscard]] QcItemStack worn(QcServerState &state, const QcItemStack &tool);
/*! Returns whether unbreaking keeps a worn armour piece \a stack from wearing by a hit (dice
    from \a state). */
[[nodiscard]] bool armourKeeps(QcServerState &state, const QcItemStack &stack);

} // namespace QcToolWear
