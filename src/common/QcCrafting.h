#pragma once

#include "QcContentRegistry.h"
#include "QcItemStack.h"

#include <QList>

#include <optional>

// Recipe matching shared by server (authoritative) and client (preview).
namespace QcCrafting {

// Index of the recipe that a square grid (size x size, row-major) of stacks produces, if any.
// Shaped recipes may sit anywhere in the grid and may be mirrored horizontally.
std::optional<qsizetype> match(const QcContentRegistry &content, const QList<QcItemStack> &grid, int size);

// Items a recipe consumes once, as item name -> count.
QList<std::pair<QString, int>> requirements(const QcRecipeDefinition &recipe);

// Side length of the smallest grid that holds the recipe (2 fits the inventory, 3 needs a table).
int gridSizeNeeded(const QcRecipeDefinition &recipe);

} // namespace QcCrafting
