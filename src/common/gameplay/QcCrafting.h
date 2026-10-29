// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcInventory.h"
#include "common/gameplay/QcItemStack.h"

#include <QList>

#include <optional>

// Recipe matching shared by server (authoritative) and client (preview).
namespace QcCrafting {

// Index of the recipe that a square grid (size x size, row-major) of stacks produces, if any.
// Shaped recipes may sit anywhere in the grid and may be mirrored horizontally.
[[nodiscard]] std::optional<qsizetype> match(const QcContentRegistry &content, const QList<QcItemStack> &grid, int size);

// Items a recipe consumes once, as ingredient (item name or "#tag") -> count; named items come
// before tags, so a tag never takes an item a named ingredient needs.
QList<std::pair<QString, int>> requirements(const QcRecipeDefinition &recipe);
// The same, with the tags ordered from the narrowest (fewest items of the content carry it) to the
// widest: an item with two tags goes to the narrower one, which has fewer alternatives.
QList<std::pair<QString, int>> requirements(const QcContentRegistry &content, const QcRecipeDefinition &recipe);
// Removes the ingredients of `times` crafts in the order of requirements(content, recipe); false (and
// possibly some removed) when they are not all there, so callers work on a copy. Server and client
// use this. Taken ingredients leave their container (a milk bucket its empty bucket, decision E-771);
// false as well when there is no room for it.
bool consume(const QcContentRegistry &content, QcInventory &inventory, const QcRecipeDefinition &recipe, int times);
// The containers the items taken from `before` to leave `after` give back, one stack per container
// item and taken item.
[[nodiscard]] QList<QcItemStack> containersLeft(const QcContentRegistry &content, const QcInventory &before, const QcInventory &after);
// The container an item leaves when crafted with (its `emptiesTo`), if any.
[[nodiscard]] std::optional<QcItemId> remainderOf(const QcContentRegistry &content, QcItemId item);
// How often (at most `limit`) the inventory holds the ingredients of the recipe, as consume() takes them.
int timesCraftable(const QcContentRegistry &content, const QcInventory &inventory, const QcRecipeDefinition &recipe, int limit);

// Whether an item fits an ingredient: the item of that name, or any item with the tag of "#tag".
bool matches(const QcContentRegistry &content, QcItemId item, const QString &ingredient);
// How many items of the inventory fit an ingredient.
int available(const QcContentRegistry &content, const QcInventory &inventory, const QString &ingredient);
// Removes `count` items that fit the ingredient; false (and possibly some removed) when there are
// fewer, so callers work on a copy.
bool removeIngredient(const QcContentRegistry &content, QcInventory &inventory, const QString &ingredient, int count);

// Side length of the smallest grid that holds the recipe (2 fits the inventory, 3 needs a table).
int gridSizeNeeded(const QcRecipeDefinition &recipe);

} // namespace QcCrafting
