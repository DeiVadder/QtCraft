#include "server/machines/QcInventoryAccess.h"

#include "common/gameplay/QcPortableContainers.h"
#include "server/core/QcServerState.h"

#include <algorithm>
#include <numeric>
#include <tuple>

namespace {

bool sameItem(const QcItemStack &a, const QcItemStack &b)
{
    return a.stacksWith(b);
}

bool before(QcBlockPos a, QcBlockPos b)
{
    return std::tuple{a.y, a.z, a.x} < std::tuple{b.y, b.z, b.x};
}

} // namespace

QcInventoryAccess::QcInventoryAccess(QcServerState &state)
    : m_state(state)
{
}

bool QcInventoryAccess::hasInventory(QcBlockPos pos) const
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    return block && (m_state.blockEntities().containerSlots(*block) > 0 || m_state.machineRules().machineOf(*block));
}

bool QcInventoryAccess::ensure(QcBlockPos pos)
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    return block && m_state.blockEntities().ensure(pos, *block);
}

QList<QcItemStack> QcInventoryAccess::slotsAt(QcBlockPos pos) const
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    if (!block) {
        return {};
    }
    if (const int cells = m_state.blockEntities().containerSlots(*block); cells > 0) {
        const QcBlockEntities::Container *container = m_state.blockEntities().container(pos);
        return container ? container->stacks : QList<QcItemStack>(cells);
    }
    if (m_state.machineRules().machineOf(*block)) {
        const QcBlockEntities::Machine *machine = m_state.blockEntities().machine(pos);
        return machine ? QList<QcItemStack>(machine->stacks.begin(), machine->stacks.end()) : QList<QcItemStack>(QcMachineRules::slotCount);
    }
    return {};
}

QList<int> QcInventoryAccess::slotsIn(QcBlockPos pos, QcBlockFace face) const
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    if (block && m_state.machineRules().machineOf(*block)) {
        return {face == QcBlockFace::Up ? QcMachineRules::inputSlot : QcMachineRules::fuelSlot};
    }

    QList<int> cells(slotsAt(pos).size());
    std::ranges::iota(cells, 0);
    return cells;
}

QList<int> QcInventoryAccess::slotsOut(QcBlockPos pos, QcBlockFace face) const
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    if (block && m_state.machineRules().machineOf(*block)) {
        // Downwards the output, and an empty bucket left in the fuel slot (as in the reference).
        return face == QcBlockFace::Down ? QList<int>{QcMachineRules::outputSlot, QcMachineRules::fuelSlot} : QList<int>{};
    }

    QList<int> cells(slotsAt(pos).size());
    std::ranges::iota(cells, 0);
    return cells;
}

bool QcInventoryAccess::mayTakeOut(QcBlockPos pos, int slot, const QcItemStack &stack) const
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    if (block && m_state.machineRules().machineOf(*block) && slot == QcMachineRules::fuelSlot) {
        return m_state.content().isValidItem(stack.item) && m_state.content().item(stack.item).scoops;
    }
    return true;
}

bool QcInventoryAccess::accepts(QcBlockPos pos, int slot, const QcItemStack &stack) const
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    // Hoppers fill and empty a backpack but never put anything with contents into it (decision E-832).
    if (block && m_state.blockEntities().isPortable(*block)) {
        return QcPortableContainers::mayHold(m_state.content(), stack);
    }
    const QcMachineDefinition *machine = block ? m_state.machineRules().machineOf(*block) : nullptr;
    if (machine && slot == QcMachineRules::fuelSlot) {
        return m_state.machineRules().fuelSeconds(stack.item) > 0.0 && !stack.hasContents();
    }
    // A machine that smelts only some kinds (smoker, blast furnace, decision E-770) takes only what it
    // smelts, so nothing stalls its input; the furnace takes anything, as in the reference.
    if (machine && slot == QcMachineRules::inputSlot && !machine->kinds.isEmpty()) {
        return m_state.machineRules().recipeFor(*machine, stack.item) != nullptr;
    }
    return true;
}

int QcInventoryAccess::slotFor(QcBlockPos pos, const QList<QcItemStack> &stacks, const QList<int> &cells, const QcItemStack &stack) const
{
    if (stack.isEmpty() || !m_state.content().isValidItem(stack.item)) {
        return -1;
    }

    const int maxStack = m_state.content().item(stack.item).maxStack;
    for (const int slot : cells) {
        const QcItemStack &there = stacks[slot];
        const bool room = there.isEmpty() || (sameItem(there, stack) && there.count < maxStack);
        if (room && accepts(pos, slot, stack)) {
            return slot;
        }
    }
    return -1;
}

QList<std::pair<int, int>> QcInventoryAccess::fit(QcBlockPos pos, QList<QcItemStack> stacks, const QList<int> &cells, const QcItemStack &stack) const
{
    const int maxStack = m_state.content().isValidItem(stack.item) ? stack.stackLimit(m_state.content().item(stack.item).maxStack) : 0;
    QList<std::pair<int, int>> places;
    int placed = 0;
    while (placed < stack.count) {
        const int slot = slotFor(pos, stacks, cells, stack);
        if (slot < 0) {
            break;
        }
        QcItemStack &there = stacks[slot];
        const int count = there.isEmpty() ? 0 : there.count;
        const int moved = std::min(maxStack - count, stack.count - placed);
        there = stack.withCount(count + moved);
        places.append({slot, moved});
        placed += moved;
    }
    return places;
}

QList<QcInventoryAccess::Insertion> QcInventoryAccess::resolve(QList<Insertion> insertions) const
{
    std::ranges::stable_sort(insertions, [](const Insertion &a, const Insertion &b) { return before(a.target, b.target); });

    QList<Insertion> kept;
    for (qsizetype first = 0; first < insertions.size();) {
        qsizetype last = first;
        while (last < insertions.size() && insertions[last].target == insertions[first].target) {
            ++last;
        }
        kept.append(resolveTarget(insertions.mid(first, last - first)));
        first = last;
    }
    return kept;
}

QList<QcInventoryAccess::Insertion> QcInventoryAccess::resolveTarget(QList<Insertion> group) const
{
    // From above first, then the order of the stacks with their data (QcItemStack::compare), so
    // equal items sit together; only these properties decide, never a direction.
    std::ranges::stable_sort(group, [](const Insertion &a, const Insertion &b) {
        if ((a.face != QcBlockFace::Up) != (b.face != QcBlockFace::Up)) {
            return a.face == QcBlockFace::Up;
        }
        return QcItemStack::compare(a.stack.withCount(1), b.stack.withCount(1)) < 0;
    });

    const QcBlockPos target = group.constFirst().target;
    QList<QcItemStack> stacks = slotsAt(target);
    QList<Insertion> kept;
    for (qsizetype first = 0; first < group.size();) {
        qsizetype last = first + 1;
        const bool above = group[first].face == QcBlockFace::Up;
        while (!above && last < group.size() && group[last].face != QcBlockFace::Up && sameItem(group[last].stack, group[first].stack)) {
            ++last;
        }

        // The group goes in whole or not at all: equal items from several sides are never chosen by
        // their direction.
        QList<QcItemStack> trial = stacks;
        QList<Insertion> placed;
        for (qsizetype i = first; i < last; ++i) {
            Insertion insertion = group[i];
            insertion.slot = slotFor(target, trial, slotsIn(target, insertion.face), insertion.stack);
            if (insertion.slot < 0) {
                placed.clear();
                break;
            }
            const QcItemStack there = trial[insertion.slot];
            trial[insertion.slot] = insertion.stack.withCount((there.isEmpty() ? 0 : there.count) + insertion.stack.count);
            placed.append(insertion);
        }
        if (!placed.isEmpty()) {
            stacks = trial;
            kept.append(placed);
        }
        first = last;
    }
    return kept;
}

QcItemStack *QcInventoryAccess::slotPtr(QcBlockPos pos, int slot)
{
    const std::optional<QcBlockId> block = m_state.world().blockAt(pos);
    if (!block || slot < 0) {
        return nullptr;
    }
    if (m_state.blockEntities().containerSlots(*block) > 0) {
        QcBlockEntities::Container *container = m_state.blockEntities().container(pos);
        return container && slot < container->stacks.size() ? &container->stacks[slot] : nullptr;
    }
    QcBlockEntities::Machine *machine = m_state.machineRules().machineOf(*block) ? m_state.blockEntities().machine(pos) : nullptr;
    return machine && slot < QcMachineRules::slotCount ? &machine->stacks[std::size_t(slot)] : nullptr;
}

bool QcInventoryAccess::take(QcBlockPos pos, int slot, int count)
{
    QcItemStack *stack = slotPtr(pos, slot);
    if (!stack) {
        return false;
    }

    // Left at count 0 until finish(): an insertion of the same item in this step still fits.
    stack->count -= count;
    m_changed.insert(pos);
    return true;
}

bool QcInventoryAccess::put(QcBlockPos pos, int slot, const QcItemStack &stack)
{
    QcItemStack *there = slotPtr(pos, slot);
    if (!there) {
        return false;
    }

    if (std::ranges::all_of(slotsAt(pos), [](const QcItemStack &other) { return other.count <= 0 || other.item == qcNoItem; })) {
        m_filledFromEmpty.insert(pos);
    }
    const int count = there->stacksWith(stack) ? there->count : 0;
    *there = stack.withCount(count + stack.count);
    m_changed.insert(pos);
    return true;
}

void QcInventoryAccess::finish()
{
    QList<QcBlockPos> changed(m_changed.begin(), m_changed.end());
    std::ranges::sort(changed, before);
    for (const QcBlockPos &pos : std::as_const(changed)) {
        for (int slot = 0; QcItemStack *stack = slotPtr(pos, slot); ++slot) {
            if (stack->count <= 0) {
                *stack = {};
            }
        }
        m_state.noteContents(pos);
    }
    m_changed.clear();
    m_filledFromEmpty.clear();
}
