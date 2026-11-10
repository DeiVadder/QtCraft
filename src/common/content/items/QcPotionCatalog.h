// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcItemDefinition.h"
#include "common/content/items/QcPotionDefinition.h"
#include "common/content/registry/QcNamedList.h"

#include <QHash>

#include <optional>

/*! \brief The kinds of potions and the brewing recipes of the content (part of
    QcContentRegistry, decision E-1061), each replaced by name, and which item is which potion.

    Server side: the client knows potions only as the items QcPotionGenerator makes of them. */
class QcPotionCatalog
{
public:
    /*! \brief The potion and form of an item. \qcfields */
    struct ItemPotion
    {
        QString potion;
        QcPotionForm form = QcPotionForm::Drink;
    };

    /*! Adds the potion \a definition, replacing one of the same name. */
    void registerPotion(QcPotionDefinition definition) { m_potions.set(std::move(definition)); }
    /*! Returns the potions in registration order. */
    const QList<QcPotionDefinition> &potions() const { return m_potions.items(); }
    /*! Returns the potion named \a name, or null if there is none. */
    const QcPotionDefinition *potion(const QString &name) const { return m_potions.find(name); }

    /*! Adds the brewing \a recipe, replacing one of the same name. */
    void registerRecipe(QcBrewingRecipe recipe) { m_recipes.set(std::move(recipe)); }
    /*! Returns the brewing recipes in registration order. */
    const QList<QcBrewingRecipe> &recipes() const { return m_recipes.items(); }

    /*! Notes that \a item is the \a form of \a potion, and which item that form has. */
    void linkItem(QcItemId item, const QString &potion, QcPotionForm form);
    /*! Returns the potion and form of \a item; nothing for an item that is no potion. */
    [[nodiscard]] std::optional<ItemPotion> potionOf(QcItemId item) const;
    /*! Returns the item of \a potion in \a form; nothing when it has none. */
    [[nodiscard]] std::optional<QcItemId> itemOf(const QString &potion, QcPotionForm form) const;

private:
    QcNamedList<QcPotionDefinition> m_potions;
    QcNamedList<QcBrewingRecipe> m_recipes;
    QHash<QcItemId, ItemPotion> m_potionOfItem;
    QHash<QString, std::array<QcItemId, qcPotionFormCount>> m_itemsOfPotion;
};
