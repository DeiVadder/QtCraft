// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcInventory.h"
#include "common/gameplay/QcItemStack.h"

#include <QList>

#include <optional>

/*! \brief Recipe matching shared by server (authoritative) and client (preview). */
namespace QcCrafting {

/*! Returns the index of the recipe of \a content that the square \a grid (\a size x \a size,
    row-major) of stacks produces, if any. Shaped recipes may sit anywhere in the grid and may be
    mirrored horizontally. */
[[nodiscard]] std::optional<qsizetype> match(const QcContentRegistry &content, const QList<QcItemStack> &grid, int size);

/*! Returns the items \a recipe consumes once, as ingredient (item name or "#tag") -> count; named
    items come before tags, so a tag never takes an item a named ingredient needs. */
QList<std::pair<QString, int>> requirements(const QcRecipeDefinition &recipe);
/*! Returns the same for \a recipe, with the tags ordered from the narrowest (fewest items of
    \a content carry it) to the widest: an item with two tags goes to the narrower one, which has
    fewer alternatives. */
QList<std::pair<QString, int>> requirements(const QcContentRegistry &content, const QcRecipeDefinition &recipe);
/*! Removes the ingredients of \a times crafts of \a recipe from \a inventory in the order of
    requirements(\a content, \a recipe). Returns false (and possibly some removed) when they are
    not all there, so callers work on a copy. Server and client use this. Taken ingredients leave
    their container (a milk bucket its empty bucket, decision E-771); false as well when there is
    no room for it. */
bool consume(const QcContentRegistry &content, QcInventory &inventory, const QcRecipeDefinition &recipe, int times);
/*! Returns the containers the items taken from \a before to leave \a after give back, one stack
    per container item and taken item, as \a content defines them. */
[[nodiscard]] QList<QcItemStack> containersLeft(const QcContentRegistry &content, const QcInventory &before, const QcInventory &after);
/*! Returns the container \a item of \a content leaves when crafted with (its \c {emptiesTo}), if
    any. */
[[nodiscard]] std::optional<QcItemId> remainderOf(const QcContentRegistry &content, QcItemId item);
/*! Returns how often (at most \a limit) \a inventory holds the ingredients of \a recipe, as
    consume() takes them with the items of \a content. */
int timesCraftable(const QcContentRegistry &content, const QcInventory &inventory, const QcRecipeDefinition &recipe, int limit);

/*! Returns whether \a item of \a content fits \a ingredient: the item of that name, or any item
    with the tag of "#tag". */
bool matches(const QcContentRegistry &content, QcItemId item, const QString &ingredient);
/*! Returns how many items of \a inventory fit \a ingredient (items of \a content). */
int available(const QcContentRegistry &content, const QcInventory &inventory, const QString &ingredient);
/*! Removes \a count items that fit \a ingredient from \a inventory (items of \a content). Returns
    false (and possibly some removed) when there are fewer, so callers work on a copy. */
bool removeIngredient(const QcContentRegistry &content, QcInventory &inventory, const QString &ingredient, int count);

/*! Returns the side length of the smallest grid that holds \a recipe (2 fits the inventory, 3
    needs a table). */
int gridSizeNeeded(const QcRecipeDefinition &recipe);

} // namespace QcCrafting
