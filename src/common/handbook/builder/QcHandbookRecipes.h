#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/handbook/QcHandbookEntry.h"

#include <QString>

class QcHandbookIndex;
class QcHandbookLinks;
class QcHandbookText;

// Recipes as shown in the handbook, and the sections every item page has about how the item is
// made and what it is used for.
class QcHandbookRecipes
{
public:
    using Recipe = QcHandbookEntry::Recipe;

    QcHandbookRecipes(const QcContentRegistry &content, const QcHandbookText &text, const QcHandbookLinks &links,
                      const QcHandbookIndex &index);

    Recipe craftingRecipe(const QcRecipeDefinition &recipe) const;
    Recipe machineRecipe(const QcSmeltingRecipe &recipe) const;
    // Crafting, smelting, uses in recipes, drops and catching of `item`, as far as there are any.
    void addItemSections(QcHandbookEntry &entry, const QString &item) const;

private:
    void addShapedCells(Recipe &result, const QcRecipeDefinition &recipe) const;
    void addShapelessCells(Recipe &result, const QcRecipeDefinition &recipe) const;
    void addMadeSections(QcHandbookEntry &entry, const QString &item) const;
    void addUsageSections(QcHandbookEntry &entry, const QString &item) const;

    const QcContentRegistry &m_content;
    const QcHandbookText &m_text;
    const QcHandbookLinks &m_links;
    const QcHandbookIndex &m_index;
};
