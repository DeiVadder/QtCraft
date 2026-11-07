// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/machines/QcBrewingRules.h"

#include <algorithm>

namespace {

// Brewing adds up tick durations that are not exact in binary; a remainder below this is done.
constexpr double timeSlack = 1e-9;

} // namespace

QcBrewingRules::QcBrewingRules(std::shared_ptr<const QcContentRegistry> content)
    : m_content(std::move(content))
{
    for (const QcBrewingRecipe &recipe : m_content->potionCatalog().recipes()) {
        addRecipe(recipe);
    }
}

void QcBrewingRules::addStep(QcItemId bottle, QcItemId ingredient, std::optional<QcItemId> result)
{
    if (result) {
        m_steps[ingredient].insert(bottle, *result);
        m_ingredients.insert(ingredient);
    }
}

std::optional<QcItemId> QcBrewingRules::resultOf(const QcBrewingRecipe &recipe, const QcPotionDefinition &potion, QcPotionForm form) const
{
    const QcPotionCatalog &catalog = m_content->potionCatalog();
    if (!recipe.base.isEmpty()) {
        return recipe.base == potion.name ? catalog.itemOf(recipe.result, form) : std::nullopt;
    }
    if (!recipe.variant.isEmpty()) {
        const auto variant = std::ranges::find_if(catalog.potions(), [&](const QcPotionDefinition &other) {
            return other.variantOf == potion.name && other.variant == recipe.variant;
        });
        return variant == catalog.potions().end() ? std::nullopt : catalog.itemOf(variant->name, form);
    }
    return form == QcPotionForm::Drink ? catalog.itemOf(potion.name, recipe.form) : std::nullopt;
}

void QcBrewingRules::addRecipe(const QcBrewingRecipe &recipe)
{
    const std::optional<QcItemId> ingredient = m_content->itemId(recipe.ingredient);
    if (!ingredient) {
        return;
    }

    const QcPotionCatalog &catalog = m_content->potionCatalog();
    for (const QcPotionDefinition &potion : catalog.potions()) {
        for (const QcPotionForm form : potion.forms) {
            const std::optional<QcItemId> bottle = catalog.itemOf(potion.name, form);
            if (bottle && !m_steps.value(*ingredient).contains(*bottle)) {
                addStep(*bottle, *ingredient, resultOf(recipe, potion, form));
            }
        }
    }
}

std::optional<QcItemId> QcBrewingRules::mix(QcItemId bottle, QcItemId ingredient) const
{
    const auto steps = m_steps.constFind(ingredient);
    if (steps == m_steps.constEnd()) {
        return std::nullopt;
    }
    const auto step = steps->constFind(bottle);
    return step == steps->constEnd() ? std::nullopt : std::optional(*step);
}

bool QcBrewingRules::accepts(const QcBrewerDefinition &brewer, int slot, const QcItemStack &stack) const
{
    if (stack.hasContents()) {
        return false;
    }
    if (slot == ingredientSlot) {
        return isIngredient(stack.item);
    }
    return slot == fuelSlot ? isFuel(brewer, stack.item) : isPotion(stack.item);
}

bool QcBrewingRules::canBrew(const Machine &machine) const
{
    const QcItemStack &ingredient = machine.stacks[ingredientSlot];
    if (ingredient.isEmpty()) {
        return false;
    }
    const auto bottles = machine.stacks.sliced(firstBottleSlot, bottleSlots);
    return std::ranges::any_of(bottles, [&](const QcItemStack &bottle) { return !bottle.isEmpty() && mix(bottle.item, ingredient.item); });
}

void QcBrewingRules::finishBrew(Machine &machine) const
{
    const QcItemId ingredient = machine.stacks[ingredientSlot].item;
    for (int slot = firstBottleSlot; slot < firstBottleSlot + bottleSlots; ++slot) {
        const QcItemStack &bottle = machine.stacks[slot];
        const std::optional<QcItemId> result = bottle.isEmpty() ? std::nullopt : mix(bottle.item, ingredient);
        if (result) {
            machine.stacks[slot] = QcItemStack{*result, bottle.count};
        }
    }

    QcItemStack &used = machine.stacks[ingredientSlot];
    used = used.count > 1 ? used.withCount(used.count - 1) : QcItemStack();
}

QcBrewingRules::Step QcBrewingRules::step(Machine &machine, const QcBrewerDefinition &brewer, double dt) const
{
    Step result = Step::Nothing;
    // The fuel goes in as soon as the stand has none left, with work or without.
    QcItemStack &fuel = machine.stacks[fuelSlot];
    if (machine.burnRemaining <= 0.0 && !fuel.isEmpty() && isFuel(brewer, fuel.item)) {
        fuel = fuel.count > 1 ? fuel.withCount(fuel.count - 1) : QcItemStack();
        machine.burnRemaining = brewer.fuelUses;
        machine.burnSeconds = brewer.fuelUses;
        result = Step::SlotsChanged;
    }

    const bool brewable = canBrew(machine);
    if (machine.progress > 0.0) {
        if (!brewable || machine.stacks[ingredientSlot].item != machine.progressItem) {
            machine.progress = 0.0;
            return result;
        }
        machine.progress += dt;
        if (machine.progress >= brewer.seconds - timeSlack) {
            machine.progress = 0.0;
            finishBrew(machine);
            return Step::Brewed;
        }
        return result;
    }

    if (brewable && machine.burnRemaining > 0.0) {
        machine.burnRemaining -= 1.0;
        machine.progress = dt;
        machine.progressItem = machine.stacks[ingredientSlot].item;
    }
    // Nothing left: a stand without fuel and work is cold again.
    if (machine.burnRemaining <= 0.0) {
        machine.burnSeconds = 0.0;
    }
    return result;
}

QcBrewingRules::Changes QcBrewingRules::tick(QcBlockEntities &entities, const QcBlockAccess &world, double dt) const
{
    Changes changes;
    entities.forEachMachine([&](Machine &machine) {
        if (machine.stacks.size() != QcBrewerDefinition::slotCount || machine.isCold()) {
            return;
        }

        const std::optional<QcBlockId> block = world.blockAt(machine.pos);
        const QcMachineDefinition *definition = block && m_content->isValidBlock(*block) ? &m_content->block(*block).machine : nullptr;
        if (!definition || !definition->brewing) {
            return;
        }

        const Step step = this->step(machine, *definition->brewing, dt);
        if (step != Step::Nothing) {
            changes.slotsChanged.append(machine.pos);
        }
        if (step == Step::Brewed) {
            changes.brewed.append(machine.pos);
        }
    });
    return changes;
}
