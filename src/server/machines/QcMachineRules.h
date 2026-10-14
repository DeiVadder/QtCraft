#pragma once

#include "QcBlockAccess.h"
#include "QcBlockEntities.h"
#include "QcContentRegistry.h"

#include <QHash>
#include <QList>

#include <memory>

// How machine blocks (furnaces) work (decisions E-037, E-038); their state lives in the block
// entities (QcBlockEntities::Machine: input, fuel and output slot, the fuel burning and the progress
// of the current item).
//
// A machine burns one fuel item whenever it can work (a recipe of its category for the input and
// room in the output) and nothing burns; burning time is the fuel's `fuelSeconds`. While fuel
// burns, the input item progresses at the machine's speed; without fuel the progress falls back
// twice as fast. Only machines in loaded chunks tick.
class QcMachineRules
{
public:
    using Machine = QcBlockEntities::Machine;
    static constexpr int inputSlot = Machine::inputSlot;
    static constexpr int fuelSlot = Machine::fuelSlot;
    static constexpr int outputSlot = Machine::outputSlot;
    static constexpr int slotCount = Machine::slotCount;

    explicit QcMachineRules(std::shared_ptr<const QcContentRegistry> content);

    // The machine definition of a block, or null when it is no machine.
    const QcMachineDefinition *machineOf(QcBlockId block) const;
    const QcSmeltingRecipe *recipeFor(const QcMachineDefinition &machine, QcItemId input) const;
    double fuelSeconds(QcItemId item) const;

    // Share of the current fuel left and of the current item done, both in [0, 1].
    double burnFraction(const Machine &machine) const;
    double progressFraction(const Machine &machine, QcBlockId block) const;

    // Advances all machines in loaded chunks. `changed` collects the positions whose slots or timers
    // changed, `slotsChanged` those whose slots changed (comparators and hoppers look again).
    void tick(QcBlockEntities &entities, const QcBlockAccess &world, double dt, QList<QcBlockPos> *changed = nullptr,
              QList<QcBlockPos> *slotsChanged = nullptr) const;
    // One machine for `dt` seconds; true when anything of it changed.
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
    // Recipe per category and input item, the first one in registry order.
    QHash<QString, QHash<QcItemId, const QcSmeltingRecipe *>> m_recipes;
};
