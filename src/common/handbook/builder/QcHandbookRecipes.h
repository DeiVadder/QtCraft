// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/handbook/QcHandbookEntry.h"

#include <QString>

class QcHandbookIndex;
class QcHandbookLinks;
class QcHandbookText;

/*! \brief Recipes as shown in the handbook, and the sections every item page has about how the
    item is made and what it is used for. */
class QcHandbookRecipes
{
public:
    /*! A recipe as a page shows it. */
    using Recipe = QcHandbookEntry::Recipe;

    QcHandbookRecipes(const QcContentRegistry &content, const QcHandbookText &text, const QcHandbookLinks &links,
                      const QcHandbookIndex &index);

    /*! Returns the crafting \a recipe as a page shows it. */
    Recipe craftingRecipe(const QcRecipeDefinition &recipe) const;
    /*! Returns the smelting \a recipe as a page shows it, with the machines that process it. */
    Recipe machineRecipe(const QcSmeltingRecipe &recipe) const;
    /*! Returns whether a machine taking \a kinds smelts its input by \a recipe: it takes the
        recipe's kind and no earlier recipe of the category for the same input
        (QcMachineRules::recipeFor). */
    [[nodiscard]] bool usedBy(const QStringList &kinds, const QcSmeltingRecipe &recipe) const;
    /*! Returns the sections about crafting, smelting, uses in recipes, drops and catching of
        \a item, as far as there are any. */
    [[nodiscard]] QList<QcHandbookEntry::Section> itemSections(const QString &item) const;

private:
    // A crafting recipe with only its grid: size, cells and whether it needs a crafting table.
    [[nodiscard]] Recipe shapedGrid(const QcRecipeDefinition &recipe) const;
    [[nodiscard]] Recipe shapelessGrid(const QcRecipeDefinition &recipe) const;
    [[nodiscard]] QList<QcHandbookEntry::Section> madeSections(const QString &item) const;
    [[nodiscard]] QList<QcHandbookEntry::Section> usageSections(const QString &item) const;
    // What the stonecutter cuts `item` from and into (decision E-784).
    [[nodiscard]] QList<QcHandbookEntry::Section> stonecuttingSections(const QString &item) const;
    [[nodiscard]] Recipe stonecuttingRecipe(const QcSmeltingRecipe &recipe) const;

    const QcContentRegistry &m_content;
    const QcHandbookText &m_text;
    const QcHandbookLinks &m_links;
    const QcHandbookIndex &m_index;
};
