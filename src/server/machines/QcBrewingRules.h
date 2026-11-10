// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/world/QcBlockAccess.h"
#include "server/blockentities/QcBlockEntities.h"

#include <QHash>
#include <QList>
#include <QSet>

#include <memory>
#include <optional>

/*! \brief How brewing stands work (decisions E-1062, E-1063); their state is a machine entry with
    five slots (QcBlockEntities::Machine).

    A stand with no fuel left takes one fuel item, good for \c {fuelUses} brews, whether or not it
    has work. A brew begins when the ingredient turns the potion of at least one bottle slot into
    another, and costs one use; after \c {seconds} one ingredient is used up and every bottle it
    fits changes. Taking the ingredient away, changing it or taking the last fitting bottle stops
    the brew. The steps come from the brewing recipes of the content. */
class QcBrewingRules
{
public:
    /*! The state of a stand. */
    using Machine = QcBlockEntities::Machine;
    /*! The ingredient and fuel slots, the first bottle slot and the number of bottle slots.
        \qcshared */
    static constexpr int ingredientSlot = Machine::inputSlot;
    static constexpr int fuelSlot = Machine::fuelSlot;
    static constexpr int firstBottleSlot = QcBrewerDefinition::firstBottleSlot;
    static constexpr int bottleSlots = QcBrewerDefinition::slotCount - firstBottleSlot;

    /*! \brief What a step did to a stand. */
    enum class Step {
        Nothing, // Nothing in a slot changed.
        SlotsChanged, // Fuel went into the stand.
        Brewed, // A brew ended: the bottles changed.
    };

    /*! \brief The stands that changed in a tick. \qcfields */
    struct Changes
    {
        QList<QcBlockPos> slotsChanged;
        QList<QcBlockPos> brewed;
    };

    explicit QcBrewingRules(std::shared_ptr<const QcContentRegistry> content);

    /*! Returns what \a ingredient makes of the potion item \a bottle; nothing when it makes
        nothing. */
    [[nodiscard]] std::optional<QcItemId> mix(QcItemId bottle, QcItemId ingredient) const;
    /*! Returns whether some recipe uses \a item as its ingredient. */
    bool isIngredient(QcItemId item) const { return m_ingredients.contains(item); }
    /*! Returns whether \a item goes into a bottle slot: a potion in any form. */
    bool isPotion(QcItemId item) const { return m_content->potionCatalog().potionOf(item).has_value(); }
    /*! Returns whether \a item is the fuel of a stand of \a brewer. */
    bool isFuel(const QcBrewerDefinition &brewer, QcItemId item) const { return m_content->itemId(brewer.fuel) == item; }
    /*! Returns whether \a stack goes into \a slot of a stand of \a brewer: an ingredient, the
        fuel, a potion; nothing with contents. */
    bool accepts(const QcBrewerDefinition &brewer, int slot, const QcItemStack &stack) const;
    /*! Returns whether the ingredient of \a machine changes one of its bottles. */
    bool canBrew(const Machine &machine) const;

    /*! Runs the stand \a machine of \a brewer for \a dt seconds. Returns what changed. */
    Step step(Machine &machine, const QcBrewerDefinition &brewer, double dt) const;
    /*! Advances all brewing stands of \a entities in loaded chunks of \a world by \a dt seconds.
        Returns the stands that changed. */
    [[nodiscard]] Changes tick(QcBlockEntities &entities, const QcBlockAccess &world, double dt) const;

private:
    // The steps of `recipe`, as potion item -> result.
    void addRecipe(const QcBrewingRecipe &recipe);
    void addStep(QcItemId bottle, QcItemId ingredient, std::optional<QcItemId> result);
    // What `recipe` makes of `potion` in `form`, if it fits it.
    [[nodiscard]] std::optional<QcItemId> resultOf(const QcBrewingRecipe &recipe, const QcPotionDefinition &potion, QcPotionForm form) const;
    // Turns the bottles of `machine` and uses up one ingredient.
    void finishBrew(Machine &machine) const;

    std::shared_ptr<const QcContentRegistry> m_content;
    // Per ingredient: what it makes of each potion item; the first recipe in load order counts.
    QHash<QcItemId, QHash<QcItemId, QcItemId>> m_steps;
    QSet<QcItemId> m_ingredients;
};
