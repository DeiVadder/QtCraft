// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcEnchantmentDefinition.h"
#include "common/gameplay/QcItemStack.h"

#include <QStringList>

class QcContentRegistry;
struct QcEquipment;
struct QcItemDefinition;

/*! \brief What enchantments do (decision E-864), pure so that server and client (mining speed it
    predicts) share the rules: which items an enchantment applies to, the level of an effect on a
    stack and the effects' numbers as in the reference game.

    Enchantments whose name the content does not know do nothing. */
namespace QcEnchanting {

/*! The highest protection points (each takes 4 % off), as in the reference. */
inline constexpr int maxProtectionPoints = 20;

/*! Returns the item kinds \a item belongs to for QcEnchantmentDefinition::items: its tool
    category and "digger" (pickaxe, axe, shovel, hoe), "weapon" (no digging tool but sword-like
    damage), "armour" and "armour_<slot>", "launcher" and "durable" (it wears). */
[[nodiscard]] QStringList kindsOf(const QcItemDefinition &item);
/*! Returns whether \a enchantment may be on \a item. */
[[nodiscard]] bool appliesTo(const QcEnchantmentDefinition &enchantment, const QcItemDefinition &item);
/*! Returns whether the enchantments \a a and \a b exclude each other (either names the other, or
    they are the same). */
[[nodiscard]] bool conflict(const QcEnchantmentDefinition &a, const QcEnchantmentDefinition &b);

/*! Returns the highest level of an enchantment of \a content with \a effect on \a stack; 0 for
    none. */
[[nodiscard]] int levelOf(const QcContentRegistry &content, const QcItemStack &stack, QcEnchantmentEffect effect);
/*! Returns the mining speed the efficiency \a level adds to a matching tool: level² + 1, none at
    0. */
[[nodiscard]] double speedBonus(int level);
/*! Returns whether a use leaves the durability (unbreaking of \a level) with \a roll in [0, 1):
    tools keep it with chance level / (level + 1), \a armour with 0.4 × level / (level + 1). */
[[nodiscard]] bool keepsDurability(int level, double roll, bool armour);
/*! Returns the melee damage sharpness of \a level adds: 0.5 × level + 0.5, none at 0. */
[[nodiscard]] double damageBonus(int level);
/*! Returns the protection points of the armour worn in \a equipment against a hit, with the
    enchantments of \a content; \a fall adds feather falling (3 per level). Protection gives its
    level per piece. At most maxProtectionPoints. */
[[nodiscard]] int protectionPoints(const QcContentRegistry &content, const QcEquipment &equipment, bool fall);
/*! Returns \a amount after \a points protection points (4 % each). */
[[nodiscard]] double reducedDamage(double amount, int points);

} // namespace QcEnchanting
