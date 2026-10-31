// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/world/QcBlockAccess.h"
#include "server/blockentities/QcBlockEntities.h"

#include <QHash>
#include <QList>

#include <memory>

/*! \brief How machine blocks (furnaces) work (decisions E-037, E-038); their state lives in the
    block entities (QcBlockEntities::Machine: input, fuel and output slot, the fuel burning and the
    progress of the current item).

    A machine burns one fuel item whenever it can work (a recipe of its category for the input
    and room in the output) and nothing burns; burning time is the fuel's \c {fuelSeconds}. While
    fuel burns, the input item progresses at the machine's speed; without fuel the progress falls
    back twice as fast. Only machines in loaded chunks tick. */
class QcMachineRules
{
public:
    /*! The state of a machine (QcBlockEntities::Machine). */
    using Machine = QcBlockEntities::Machine;
    /*! The slots of a machine, as in QcBlockEntities::Machine. \qcshared */
    static constexpr int inputSlot = Machine::inputSlot;
    static constexpr int fuelSlot = Machine::fuelSlot;
    static constexpr int outputSlot = Machine::outputSlot;
    static constexpr int slotCount = Machine::slotCount;

    explicit QcMachineRules(std::shared_ptr<const QcContentRegistry> content);

    /*! Returns the machine definition of \a block, or null when it is no machine. */
    const QcMachineDefinition *machineOf(QcBlockId block) const;
    /*! Returns the recipe of the category and kinds of \a machine for \a input, or null when
        there is none. */
    const QcSmeltingRecipe *recipeFor(const QcMachineDefinition &machine, QcItemId input) const;
    /*! Returns the recipe of \a category for \a input if its kind is among \a kinds (empty: any;
        a campfire). */
    const QcSmeltingRecipe *recipeFor(const QString &category, const QStringList &kinds, QcItemId input) const;
    /*! Returns the seconds \a item burns as fuel, 0 for no fuel. */
    double fuelSeconds(QcItemId item) const;

    /*! Returns the share of the current fuel of \a machine left, in [0, 1]. */
    double burnFraction(const Machine &machine) const;
    /*! Returns the share of the current item of \a machine (of the machine block \a block) done,
        in [0, 1]. */
    double progressFraction(const Machine &machine, QcBlockId block) const;

    /*! Advances all machines of \a entities in loaded chunks of \a world by \a dt seconds. Returns
        the positions whose slots changed (comparators and hoppers look again). */
    [[nodiscard]] QList<QcBlockPos> tick(QcBlockEntities &entities, const QcBlockAccess &world, double dt) const;
    /*! Runs \a machine of \a definition for \a dt seconds. Returns \c true when anything of it
        changed. */
    bool step(Machine &machine, const QcMachineDefinition &definition, double dt) const;

private:
    // Whether the machine's input has a recipe whose result fits into the output.
    const QcSmeltingRecipe *workableRecipe(const Machine &machine, const QcMachineDefinition &definition) const;
    // Takes one fuel item when the machine has burnt out; true when it lit.
    bool refuel(Machine &machine) const;
    // Turns the progress into finished items, at most maxItemsPerTick; returns the next workable recipe.
    const QcSmeltingRecipe *finishItems(Machine &machine, const QcMachineDefinition &definition,
                                        const QcSmeltingRecipe *recipe) const;

    std::shared_ptr<const QcContentRegistry> m_content;
    // Recipes per category and input item in registry order; a machine uses the first whose kind it
    // takes (decision E-770).
    QHash<QString, QHash<QcItemId, QList<const QcSmeltingRecipe *>>> m_recipes;
};
