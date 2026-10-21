#include "common/gameplay/QcInventory.h"

#include "common/content/registry/QcContentRegistry.h"

#include <algorithm>

QcInventory::QcInventory()
    : m_slots(slotCount)
{
}

void QcInventory::setSelectedHotbarSlot(int slot)
{
    m_selected = std::clamp(slot, 0, hotbarSize - 1);
}

int QcInventory::add(const QcItemStack &adding, const QcContentRegistry &content)
{
    int count = adding.count;
    if (!content.isValidItem(adding.item) || count <= 0) {
        return std::max(count, 0);
    }

    const int maxStack = adding.stackLimit(content.item(adding.item).maxStack);
    for (QcItemStack &stack : m_slots) {
        if (count == 0) {
            break;
        }
        if (!stack.isEmpty() && stack.stacksWith(adding) && stack.count < maxStack) {
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
            stack = adding.withCount(moved);
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

int QcInventory::ammunitionSlot(const QcContentRegistry &content, const QcLauncherDefinition &launcher) const
{
    for (int slot = 0; slot < slotCount; ++slot) {
        const QcItemStack &stack = m_slots[slot];
        if (!stack.isEmpty() && content.isValidItem(stack.item) && isAmmunition(content.item(stack.item), launcher)) {
            return slot;
        }
    }
    return -1;
}

bool QcInventory::isAmmunition(const QcItemDefinition &item, const QcLauncherDefinition &launcher)
{
    // Without a list a launcher takes every ammunition but what is thrown by hand (decision E-427).
    if (item.projectile.isEmpty() || (launcher.ammunition.isEmpty() && item.throwable)) {
        return false;
    }
    return launcher.takes(item.name, item.tags);
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

void QcInventory::move(int from, int to, const QcContentRegistry &content)
{
    if (!isValidSlot(from) || !isValidSlot(to) || from == to) {
        return;
    }

    QcItemStack &source = m_slots[from];
    QcItemStack &target = m_slots[to];
    if (!source.isEmpty() && source.stacksWith(target) && content.isValidItem(source.item)) {
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
