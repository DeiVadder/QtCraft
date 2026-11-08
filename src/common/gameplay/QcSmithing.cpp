// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/gameplay/QcSmithing.h"

#include "common/content/registry/QcContentRegistry.h"

#include <algorithm>

namespace QcSmithing {

namespace {

// Whether `stack` holds the item named `name`.
bool holds(const QcContentRegistry &content, const QcItemStack &stack, const QString &name)
{
    return !stack.isEmpty() && content.isValidItem(stack.item) && content.item(stack.item).name == name;
}

const QcItemStack &at(const Inputs &inputs, Slot slot)
{
    return inputs[static_cast<std::size_t>(slot)];
}

} // namespace

const QcSmithingRecipe *recipeFor(const QcContentRegistry &content, const Inputs &inputs)
{
    if (at(inputs, Slot::Base).count != 1) {
        return nullptr;
    }
    const auto &recipes = content.smithingRecipes();
    const auto found = std::ranges::find_if(recipes, [&](const QcSmithingRecipe &recipe) {
        return holds(content, at(inputs, Slot::Template), recipe.templateItem) && holds(content, at(inputs, Slot::Base), recipe.base)
               && holds(content, at(inputs, Slot::Addition), recipe.addition);
    });
    return found == recipes.end() ? nullptr : &*found;
}

QcItemStack output(const QcContentRegistry &content, const Inputs &inputs)
{
    const QcSmithingRecipe *recipe = recipeFor(content, inputs);
    const std::optional<QcItemId> result = recipe ? content.itemId(recipe->result) : std::nullopt;
    if (!result) {
        return {};
    }
    // The base's damage and data stay, as the reference's smithing transform copies them.
    QcItemStack made = at(inputs, Slot::Base);
    made.item = *result;
    made.count = 1;
    // A result that lasts less than the base keeps at least one use.
    const int durability = content.item(*result).durability;
    if (durability > 0) {
        made.damage = std::clamp(made.damage, 0, durability - 1);
    }
    return made;
}

std::optional<Slot> slotFor(const QcContentRegistry &content, const QcItemStack &stack)
{
    if (stack.isEmpty()) {
        return std::nullopt;
    }
    const auto &recipes = content.smithingRecipes();
    for (const auto &[slot, field] : {std::pair{Slot::Template, &QcSmithingRecipe::templateItem}, std::pair{Slot::Base, &QcSmithingRecipe::base},
                                      std::pair{Slot::Addition, &QcSmithingRecipe::addition}}) {
        if (std::ranges::any_of(recipes, [&](const QcSmithingRecipe &recipe) { return holds(content, stack, recipe.*field); })) {
            return slot;
        }
    }
    return std::nullopt;
}

} // namespace QcSmithing
