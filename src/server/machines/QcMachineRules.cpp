#include "QcMachineRules.h"

#include <algorithm>
#include <cmath>

namespace {

// Burning adds up tick durations that are not exact in binary; a remainder below this is used up.
constexpr double timeSlack = 1e-9;
// Bound for the items one machine finishes in one tick (very fast mod machines).
constexpr int maxItemsPerTick = 64;

} // namespace

QcMachineRules::QcMachineRules(std::shared_ptr<const QcContentRegistry> content)
    : m_content(std::move(content))
{
    for (const QcSmeltingRecipe &recipe : m_content->smeltingRecipes()) {
        const std::optional<QcItemId> input = m_content->itemId(recipe.inputItem);
        if (!input) {
            continue;
        }

        QHash<QcItemId, const QcSmeltingRecipe *> &category = m_recipes[recipe.category];
        if (!category.contains(*input)) {
            category.insert(*input, &recipe);
        }
    }
}

const QcMachineDefinition *QcMachineRules::machineOf(QcBlockId block) const
{
    if (!m_content->isValidBlock(block)) {
        return nullptr;
    }
    const QcMachineDefinition &machine = m_content->block(block).machine;
    return machine.isMachine() ? &machine : nullptr;
}

const QcSmeltingRecipe *QcMachineRules::recipeFor(const QcMachineDefinition &machine, QcItemId input) const
{
    const auto category = m_recipes.constFind(machine.category);
    if (category == m_recipes.constEnd()) {
        return nullptr;
    }
    return category->value(input, nullptr);
}

double QcMachineRules::fuelSeconds(QcItemId item) const
{
    return m_content->isValidItem(item) ? m_content->item(item).fuelSeconds : 0.0;
}

double QcMachineRules::burnFraction(const Machine &machine) const
{
    return machine.burnSeconds > 0.0 ? std::clamp(machine.burnRemaining / machine.burnSeconds, 0.0, 1.0) : 0.0;
}

double QcMachineRules::progressFraction(const Machine &machine, QcBlockId block) const
{
    const QcMachineDefinition *definition = machineOf(block);
    const QcSmeltingRecipe *recipe =
        definition && !machine.stacks[inputSlot].isEmpty() ? recipeFor(*definition, machine.stacks[inputSlot].item) : nullptr;
    return recipe ? std::clamp(machine.progress / recipe->seconds, 0.0, 1.0) : 0.0;
}

const QcSmeltingRecipe *QcMachineRules::workableRecipe(const Machine &machine, const QcMachineDefinition &definition) const
{
    const QcItemStack &input = machine.stacks[inputSlot];
    const QcSmeltingRecipe *recipe = input.isEmpty() ? nullptr : recipeFor(definition, input.item);
    if (!recipe) {
        return nullptr;
    }

    const std::optional<QcItemId> result = m_content->itemId(recipe->resultItem);
    if (!result) {
        return nullptr;
    }

    const QcItemStack &output = machine.stacks[outputSlot];
    if (output.isEmpty()) {
        return recipe->resultCount <= m_content->item(*result).maxStack ? recipe : nullptr;
    }

    const bool fits = output.item == *result && output.damage == 0
                      && output.count + recipe->resultCount <= m_content->item(*result).maxStack;
    return fits ? recipe : nullptr;
}

bool QcMachineRules::refuel(Machine &machine) const
{
    QcItemStack &fuel = machine.stacks[fuelSlot];
    const double seconds = fuel.isEmpty() ? 0.0 : fuelSeconds(fuel.item);
    // Fuel in a container (lava bucket) leaves the empty container in the fuel slot, so such
    // fuel only burns one at a time.
    const QString &emptiesTo = seconds > 0.0 ? m_content->item(fuel.item).emptiesTo : QString();
    const std::optional<QcItemId> container = emptiesTo.isEmpty() ? std::nullopt : m_content->itemId(emptiesTo);
    if (seconds <= 0.0 || (container && fuel.count != 1)) {
        return false;
    }

    machine.burnRemaining = machine.burnSeconds = seconds;
    if (--fuel.count <= 0) {
        fuel = container ? QcItemStack{*container, 1, 0} : QcItemStack{};
    }
    return true;
}

const QcSmeltingRecipe *QcMachineRules::finishItems(Machine &machine, const QcMachineDefinition &definition,
                                                      const QcSmeltingRecipe *recipe) const
{
    // A fast machine finishes several items in one tick; the rest of the work carries over.
    for (int finished = 0; recipe && finished < maxItemsPerTick && machine.progress >= recipe->seconds - timeSlack; ++finished) {
        machine.progress = std::max(0.0, machine.progress - recipe->seconds);
        // workableRecipe() only returns recipes whose result exists.
        const QcItemId result = m_content->itemId(recipe->resultItem).value_or(qcNoItem);
        QcItemStack &output = machine.stacks[outputSlot];
        output = {result, (output.isEmpty() ? 0 : output.count) + recipe->resultCount, 0};

        QcItemStack &input = machine.stacks[inputSlot];
        if (--input.count <= 0) {
            input = {};
        }

        recipe = workableRecipe(machine, definition);
    }
    return recipe;
}

bool QcMachineRules::step(Machine &machine, const QcMachineDefinition &definition, double dt) const
{
    const double work = dt * definition.speed;
    const QcSmeltingRecipe *recipe = workableRecipe(machine, definition);
    bool changed = false;

    if (recipe && machine.progressItem != machine.stacks[inputSlot].item) {
        machine.progressItem = machine.stacks[inputSlot].item;
        machine.progress = 0.0;
        changed = true;
    }

    if (machine.burnRemaining <= 0.0 && recipe && refuel(machine)) {
        changed = true;
    }

    if (machine.burnRemaining > 0.0) {
        machine.burnRemaining -= work;
        changed = true;
        if (machine.burnRemaining <= timeSlack) {
            machine.burnRemaining = 0.0;
            machine.burnSeconds = 0.0;
        }

        if (recipe) {
            machine.progress += work;
            recipe = finishItems(machine, definition, recipe);
            // No next item (input used up, output full): the next one starts from zero.
            machine.progress = recipe ? std::min(machine.progress, recipe->seconds) : 0.0;
            return true;
        }
    }

    if (!recipe && machine.progress > 0.0) {
        // Another input or a full output starts the item over.
        machine.progress = 0.0;
        return true;
    }

    if (machine.burnRemaining <= 0.0 && machine.progress > 0.0) {
        machine.progress = std::max(0.0, machine.progress - 2.0 * work);
        return true;
    }
    return changed;
}

void QcMachineRules::tick(QcBlockEntities &entities, const QcBlockAccess &world, double dt, QList<QcBlockPos> *changed,
                          QList<QcBlockPos> *slotsChanged) const
{
    entities.forEachMachine([&](Machine &machine) {
        // An empty, cold machine has nothing to do (generated furnaces, decision E-369); its step
        // would change nothing, so it costs no block lookup.
        if (machine.isIdle()) {
            return;
        }

        const std::optional<QcBlockId> block = world.blockAt(machine.pos);
        const QcMachineDefinition *definition = block ? machineOf(*block) : nullptr;
        if (!definition) {
            // Not loaded (a machine entry always has a machine block, QcBlockEntityChecks).
            return;
        }

        const std::array<QcItemStack, slotCount> before = machine.stacks;
        if (step(machine, *definition, dt) && changed) {
            changed->append(machine.pos);
        }
        if (slotsChanged && machine.stacks != before) {
            slotsChanged->append(machine.pos);
        }
    });
}
