// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcEnchantmentDefinition.h"
#include "common/gameplay/QcItemStack.h"

#include <QStringList>

class QcContentRegistry;
struct QcEquipment;
struct QcItemDefinition;

// What enchantments do (decision E-864), pure so that server and client (mining speed it predicts)
// share the rules: which items an enchantment applies to, the level of an effect on a stack and the
// effects' numbers as in the reference game. Enchantments whose name the content does not know do
// nothing.
namespace QcEnchanting {

// The highest protection points (each takes 4 % off), as in the reference.
inline constexpr int maxProtectionPoints = 20;

// The item kinds `item` belongs to for QcEnchantmentDefinition::items: its tool category and
// "digger" (pickaxe, axe, shovel, hoe), "weapon" (no digging tool but sword-like damage),
// "armour" and "armour_<slot>", "launcher" and "durable" (it wears).
[[nodiscard]] QStringList kindsOf(const QcItemDefinition &item);
// Whether `enchantment` may be on `item`.
[[nodiscard]] bool appliesTo(const QcEnchantmentDefinition &enchantment, const QcItemDefinition &item);
// Whether two enchantments exclude each other (either names the other, or they are the same).
[[nodiscard]] bool conflict(const QcEnchantmentDefinition &a, const QcEnchantmentDefinition &b);

// The highest level of an enchantment with `effect` on `stack`; 0 for none.
[[nodiscard]] int levelOf(const QcContentRegistry &content, const QcItemStack &stack, QcEnchantmentEffect effect);
// The mining speed an efficiency level adds to a matching tool: level² + 1, none at 0.
[[nodiscard]] double speedBonus(int level);
// Whether a use leaves the durability (unbreaking) with a roll in [0, 1): tools keep it with
// chance level / (level + 1), armour with 0.4 × level / (level + 1).
[[nodiscard]] bool keepsDurability(int level, double roll, bool armour);
// The melee damage sharpness adds: 0.5 × level + 0.5, none at 0.
[[nodiscard]] double damageBonus(int level);
// The protection points of the worn armour against a hit; `fall` adds feather falling (3 per level).
// Protection gives its level per piece. At most maxProtectionPoints.
[[nodiscard]] int protectionPoints(const QcContentRegistry &content, const QcEquipment &equipment, bool fall);
// `amount` after protection points (4 % each).
[[nodiscard]] double reducedDamage(double amount, int points);

} // namespace QcEnchanting
