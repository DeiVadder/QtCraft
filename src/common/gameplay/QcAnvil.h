// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcItemStack.h"

#include <QString>

class QcContentRegistry;
struct QcEnchantmentDefinition;

// The anvil's rules (decision E-866), pure so that tests and the server share them, as in the
// reference game: the left stack is repaired with its material (each item a quarter of the
// durability, one level each), combined with a second item of its kind (durability of both plus
// 12 %, two levels; the right item's enchantments merge in: equal levels rise by one up to the
// highest, otherwise the higher stays; conflicting or unfitting ones cost a level and stay off) and
// renamed (one level). Every use adds the prior-work penalty 2^n − 1 of both stacks and raises the
// result's count of uses by one (a rename alone does not). From tooExpensive levels on the anvil
// refuses, except a rename alone, which costs at most tooExpensive − 1. An anvil swap (decision E-870,
// QcItemDefinition::anvilSwap) turns a whole stack into its other item, paid with one `with` in the
// right slot and one level per started swapLevelItems items; it adds the prior-work penalty but keeps
// the stack's prior work. An enchanted book in the right slot (decision E-873) gives its enchantments
// to the left item, a book included, at half the cost per level (at least 1) and without repairing.
namespace QcAnvil {

// From this cost on the anvil refuses (outside creative mode), as in the reference.
inline constexpr int tooExpensive = 40;
// Items per level of an anvil swap: a full stack of 64 costs 4 levels.
inline constexpr int swapLevelItems = 16;
// The chance that an anvil wears one step on each use.
inline constexpr double wearChance = 0.12;
// Chance per block fallen (rounded up) that a falling anvil wears on a landing that hurts.
inline constexpr double landingWearPerBlock = 0.05;

// What the anvil makes of its two slots.
struct Result
{
    // Empty: nothing to do (no change, or the right stack does not fit the left one).
    QcItemStack output;
    // Levels it costs, prior-work penalty included.
    int levelCost = 0;
    // How many of the right stack's items it uses up.
    int rightUsed = 0;

    bool isEmpty() const { return output.isEmpty(); }
    // Whether it costs too much for a player outside creative mode.
    bool isTooExpensive() const { return levelCost >= tooExpensive; }
    bool operator==(const Result &) const = default;
};

// The result for `left` and `right` with `name` in the name field: the left stack's custom name
// leaves it unchanged, other text renames it, blank text takes a custom name off (and changes
// nothing on a stack without one).
[[nodiscard]] Result result(const QcContentRegistry &content, const QcItemStack &left, const QcItemStack &right, const QString &name);
// The levels the prior-work count `uses` adds: 2^uses − 1.
[[nodiscard]] int penaltyOf(int uses);
// The levels an anvil swap of `count` items costs: one per started swapLevelItems, at least one.
[[nodiscard]] int swapLevels(int count);
// The levels each level of an enchantment costs when merged, from its weight as the reference's
// rarity: 1 (weight 10 or more), 2 (5 or more), 4 (2 or more), else 8.
[[nodiscard]] int costPerLevel(const QcEnchantmentDefinition &enchantment);
// `text` as a custom name (QcNameText::clean: without control and format characters, trimmed),
// cut between graphemes to at most QcItemData::maxNameLength UTF-16 units.
[[nodiscard]] QString nameFrom(const QString &text);

} // namespace QcAnvil
