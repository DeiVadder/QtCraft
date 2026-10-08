#pragma once

#include "QcContentRegistry.h"
#include "QcInventory.h"
#include "QcItemStack.h"

#include <QList>

#include <optional>

// Recipe matching shared by server (authoritative) and client (preview).
namespace QcCrafting {

// Index of the recipe that a square grid (size x size, row-major) of stacks produces, if any.
// Shaped recipes may sit anywhere in the grid and may be mirrored horizontally.
std::optional<qsizetype> match(const QcContentRegistry &content, const QList<QcItemStack> &grid, int size);

// Items a recipe consumes once, as ingredient (item name or "#tag") -> count; named items come
// before tags, so a tag never takes an item a named ingredient needs.
QList<std::pair<QString, int>> requirements(const QcRecipeDefinition &recipe);
// Removes the ingredients of `times` crafts in the order of requirements(); false (and possibly some
// removed) when they are not all there, so callers work on a copy. Server and client use this.
bool consume(const QcContentRegistry &content, QcInventory &inventory, const QcRecipeDefinition &recipe, int times);
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
