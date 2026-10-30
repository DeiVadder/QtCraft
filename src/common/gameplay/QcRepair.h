// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcItemStack.h"

#include <QList>

#include <optional>

class QcContentRegistry;

/*! \brief Repair in the crafting grid (decision E-236): two items of the same kind with durability,
    at least one of them worn, and nothing else make one item with both remaining durabilities plus
    a bonus of 5 % of the full durability.

    Works on any grid size; tools and armour alike. */
namespace QcRepair {

/*! The bonus as a share of the full durability. */
inline constexpr double bonusShare = 0.05;

/*! Returns the repaired item for the cells \a grid, with the items of \a content, or nothing when
    they are no repair. */
[[nodiscard]] std::optional<QcItemStack> result(const QcContentRegistry &content, const QList<QcItemStack> &grid);

} // namespace QcRepair
