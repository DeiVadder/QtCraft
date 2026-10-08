#pragma once

#include "QcBlockAccess.h"
#include "QcContentRegistry.h"
#include "QcItemStack.h"

#include <QHash>
#include <QList>

#include <array>
#include <memory>

// State of the machine blocks (furnaces) on the server, one entry per block position: input, fuel
// and output slot, the fuel burning and the progress of the current item (decisions E-037, E-038).
//
// A machine burns one fuel item whenever it can work (a recipe of its category for the input and
// room in the output) and nothing burns; burning time is the fuel's `fuelSeconds`. While fuel
// burns, the input item progresses at the machine's speed; without fuel the progress falls back
// twice as fast. Only machines in loaded chunks tick; their number is limited.
class QcMachineManager
{
public:
    static constexpr int maxMachines = 4096;
    static constexpr int inputSlot = 0;
    static constexpr int fuelSlot = 1;
    static constexpr int outputSlot = 2;
    static constexpr int slotCount = 3;

    struct Machine
    {
        QcBlockPos pos;
        std::array<QcItemStack, slotCount> stacks;
        // Seconds left of the current fuel item and its full burning time.
        double burnRemaining = 0.0;
        double burnSeconds = 0.0;
        // Seconds of work done on the current input item (at speed 1), and that item; another
        // input starts over.
        double progress = 0.0;
        QcItemId progressItem = qcNoItem;
    };

    explicit QcMachineManager(std::shared_ptr<const QcContentRegistry> content);

    // The machine definition of a block, or null when it is no machine.
    const QcMachineDefinition *machineOf(QcBlockId block) const;
    const QcSmeltingRecipe *recipeFor(const QcMachineDefinition &machine, QcItemId input) const;
    double fuelSeconds(QcItemId item) const;

    Machine *find(QcBlockPos pos);
    const Machine *find(QcBlockPos pos) const;
    // Adds an empty machine at `pos` unless one exists; false when the limit is reached.
    bool add(QcBlockPos pos);
    // Removes the machine and returns it with its contents (to be dropped); empty slots when none.
    Machine remove(QcBlockPos pos);
    bool isFull() const { return m_machines.size() >= maxMachines; }
    qsizetype count() const { return m_machines.size(); }
    QList<Machine> machines() const { return m_machines.values(); }
    // Replaces all machines, e.g. from a save; entries beyond the limit are left out.
    void restore(const QList<Machine> &machines);

    // Share of the current fuel left and of the current item done, both in [0, 1].
    double burnFraction(const Machine &machine) const;
    double progressFraction(const Machine &machine, QcBlockId block) const;

    // Advances all machines in loaded chunks. Machines whose block is no machine any more are
    // removed and returned so that their contents can be dropped. `changed` collects the positions
    // whose slots or timers changed.
    QList<Machine> tick(const QcBlockAccess &world, double dt, QList<QcBlockPos> *changed = nullptr);

private:
    // Whether the machine's input has a recipe whose result fits into the output.
    const QcSmeltingRecipe *workableRecipe(const Machine &machine, const QcMachineDefinition &definition) const;
    bool step(Machine &machine, const QcMachineDefinition &definition, double dt);

    std::shared_ptr<const QcContentRegistry> m_content;
    // Recipe per category and input item, the first one in registry order.
    QHash<QString, QHash<QcItemId, const QcSmeltingRecipe *>> m_recipes;
    QHash<QcBlockPos, Machine> m_machines;
};
