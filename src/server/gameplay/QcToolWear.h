#pragma once

#include "common/gameplay/QcItemStack.h"

class QcServerState;

// One use of an item that wears (decision E-864): a tool mining, a weapon hitting, a bow shooting.
// Unbreaking lets a use leave the durability with the chance of QcEnchanting::keepsDurability,
// drawn from the server's random generator.
namespace QcToolWear {

// `tool` after one use: one more damage, empty when that breaks it; unchanged for items without
// durability or when unbreaking keeps it.
[[nodiscard]] QcItemStack worn(QcServerState &state, const QcItemStack &tool);
// Whether unbreaking keeps a worn armour piece `stack` from wearing by a hit.
[[nodiscard]] bool armourKeeps(QcServerState &state, const QcItemStack &stack);

} // namespace QcToolWear
