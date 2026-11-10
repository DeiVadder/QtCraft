// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcItemStack.h"

#include <array>
#include <optional>

class QcContentRegistry;
struct QcSmithingRecipe;

/*! \brief The smithing table's rules (decision E-1115), pure so that tests and the server share
    them, as in the reference game: a template, a base item and an addition in the three slots
    make the recipe's result, which keeps the base's damage, enchantments, name and prior work;
    taking it uses up one template, the base and one addition. */
namespace QcSmithing {

/*! The three input slots, in screen order. */
enum class Slot : quint8 {
    Template, // The smithing template.
    Base, // The item to upgrade.
    Addition // The material (a netherite ingot).
};

/*! The number of input slots. */
inline constexpr int slotCount = 3;

/*! \brief The stacks in the three input slots, by Slot. */
using Inputs = std::array<QcItemStack, slotCount>;

/*! Returns the recipe of \a content that \a inputs make; null when none fits (each slot needs its
    item, the base a single one). */
[[nodiscard]] const QcSmithingRecipe *recipeFor(const QcContentRegistry &content, const Inputs &inputs);
/*! Returns what \a inputs make in \a content: the base stack turned into the recipe's result with
    all it carries; empty when no recipe fits or the result is unknown. */
[[nodiscard]] QcItemStack output(const QcContentRegistry &content, const Inputs &inputs);
/*! Returns the slot a shift-click puts \a stack into, by what the recipes of \a content take
    there (template before base before addition); none when no recipe takes it. */
[[nodiscard]] std::optional<Slot> slotFor(const QcContentRegistry &content, const QcItemStack &stack);

} // namespace QcSmithing
