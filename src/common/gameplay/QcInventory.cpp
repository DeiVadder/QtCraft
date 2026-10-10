#include "QcInventory.h"

#include "QcContentRegistry.h"

#include <algorithm>

QcInventory::QcInventory()
    : m_slots(slotCount)
{
}

void QcInventory::setSelectedHotbarSlot(int slot)
{
    m_selected = std::clamp(slot, 0, hotbarSize - 1);
}

int QcInventory::add(QcItemId item, int count, const QcContentRegistry &content, int damage)
{
    if (!content.isValidItem(item) || count <= 0) {
        return count;
    }

    const int maxStack = content.item(item).maxStack;
    for (QcItemStack &stack : m_slots) {
        if (count == 0 || damage > 0) {
            break;
        }
        // Damaged tools never merge with other stacks.
        if (stack.item == item && stack.count < maxStack && stack.damage == 0) {
            const int moved = std::min(count, maxStack - stack.count);
            stack.count += moved;
            count -= moved;
        }
    }

    for (QcItemStack &stack : m_slots) {
        if (count == 0) {
            break;
        }
        if (stack.isEmpty()) {
            const int moved = std::min(count, maxStack);
            stack = {item, moved, damage};
            count -= moved;
        }
    }
    return count;
}

int QcInventory::removeFromSlot(int index, int count)
{
    if (!isValidSlot(index) || count <= 0) {
        return 0;
    }

    QcItemStack &stack = m_slots[index];
    const int removed = std::min(count, stack.count);
    stack.count -= removed;
    if (stack.count <= 0) {
        stack = {};
    }
    return removed;
}

int QcInventory::countOf(QcItemId item) const
{
    int total = 0;
    for (const QcItemStack &stack : m_slots) {
        if (stack.item == item) {
            total += stack.count;
        }
    }
    return total;
}

bool QcInventory::removeItems(QcItemId item, int count)
{
    if (countOf(item) < count) {
        return false;
    }

    for (int i = 0; i < slotCount && count > 0; ++i) {
        if (m_slots[i].item == item) {
            count -= removeFromSlot(i, count);
        }
    }
    return true;
}

bool QcInventory::moveBetween(QList<QcItemStack> &stacks, int from, int to, int count, const QcContentRegistry &content)
{
    if (from < 0 || to < 0 || from >= stacks.size() || to >= stacks.size() || from == to || count < 0) {
        return false;
    }

    QcItemStack &source = stacks[from];
    QcItemStack &target = stacks[to];
    if (source.isEmpty() || !content.isValidItem(source.item)) {
        return true;
    }

    const int wanted = count == 0 ? source.count : std::min(count, source.count);
    if (target.isEmpty() || (target.item == source.item && target.damage == 0 && source.damage == 0)) {
        const int space = content.item(source.item).maxStack - (target.isEmpty() ? 0 : target.count);
        const int moved = std::min(wanted, space);
        if (moved <= 0) {
            return true;
        }
        target = {source.item, (target.isEmpty() ? 0 : target.count) + moved, source.damage};
        source.count -= moved;
        if (source.count <= 0) {
            source = {};
        }
        return true;
    }

    // Different items: only whole stacks swap.
    if (count == 0) {
        std::swap(source, target);
    }
    return true;
}

int QcInventory::moveInto(QList<QcItemStack> &stacks, int from, const QList<int> &targets, const QcContentRegistry &content)
{
    if (from < 0 || from >= stacks.size() || stacks[from].isEmpty() || !content.isValidItem(stacks[from].item)) {
        return 0;
    }

    const int before = stacks[from].count;
    // Damaged items never merge; equal undamaged stacks fill up first.
    for (const bool merging : {true, false}) {
        for (const int to : targets) {
            if (stacks[from].isEmpty()) {
                break;
            }
            if (to == from || to < 0 || to >= stacks.size()) {
                continue;
            }
            const QcItemStack &target = stacks[to];
            const bool equal = !target.isEmpty() && target.item == stacks[from].item && target.damage == 0 && stacks[from].damage == 0;
            if (merging ? equal : target.isEmpty()) {
                moveBetween(stacks, from, to, 0, content);
            }
        }
    }
    return before - (stacks[from].isEmpty() ? 0 : stacks[from].count);
}

void QcInventory::move(int from, int to, const QcContentRegistry &content)
{
    if (!isValidSlot(from) || !isValidSlot(to) || from == to) {
        return;
    }

    QcItemStack &source = m_slots[from];
    QcItemStack &target = m_slots[to];
    if (!source.isEmpty() && source.item == target.item && source.damage == 0 && target.damage == 0
        && content.isValidItem(source.item)) {
        const int moved = std::min(source.count, content.item(source.item).maxStack - target.count);
        target.count += moved;
        source.count -= moved;
        if (source.count <= 0) {
            source = {};
        }
        return;
    }

    std::swap(source, target);
}
