#pragma once

#include "common/gameplay/QcItemStack.h"

#include <QList>

#include <optional>

class QcContentRegistry;

// Repair in the crafting grid (decision E-236): two items of the same kind with durability, at
// least one of them worn, and nothing else make one item with both remaining durabilities plus a
// bonus of 5 % of the full durability. Works on any grid size; tools and armour alike.
namespace QcRepair {

inline constexpr double bonusShare = 0.05;

// The repaired item for the grid cells, or nothing when they are no repair.
[[nodiscard]] std::optional<QcItemStack> result(const QcContentRegistry &content, const QList<QcItemStack> &grid);

} // namespace QcRepair
