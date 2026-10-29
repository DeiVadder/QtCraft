// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockFace.h"
#include "common/gameplay/QcItemStack.h"
#include "common/world/QcBlockAccess.h"

#include <QList>

#include <optional>

class QcBlockEntities;
class QcHangingManager;
class QcMinecartManager;

// What a Signal Comparator reads from a block (decision E-343): the fullness of a container or
// machine with the reference formula, a state property scaled to 0..15, or the fullness of a
// container cart on a detector rail (decision E-385), as the block's data says.
class QcComparatorReadings
{
public:
    QcComparatorReadings(const QcBlockAccess &world, const QcBlockEntities &entities, const QcMinecartManager &carts,
                         const QcHangingManager &hangings);

    // The reference formula: 0 when every slot is empty, else 1 + 14 times the mean fullness of the
    // slots (each stack's count over its item's maxStack), rounded down.
    static int fullness(const QcContentRegistry &content, const QList<QcItemStack> &stacks);

    // The reading of the block at `pos`, or nothing when the block is not readable.
    [[nodiscard]] std::optional<int> at(QcBlockPos pos) const;
    // The reading of the item frame in `cell` on `face` (decision E-792): the turn of its item, 1
    // to 8, or 0 while it is empty; nothing without such a frame.
    [[nodiscard]] std::optional<int> frameAt(QcBlockPos cell, QcBlockFace face) const;

private:
    int contentsAt(QcBlockPos pos) const;

    const QcBlockAccess &m_world;
    const QcBlockEntities &m_entities;
    const QcMinecartManager &m_carts;
    const QcHangingManager &m_hangings;
};
