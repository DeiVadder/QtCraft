#pragma once

#include "common/gameplay/QcItemStack.h"

#include <QList>

class QcContentRegistry;
struct QcItemDefinition;
struct QcLauncherDefinition;

// Player inventory: hotbar slots 0..8 followed by the main storage. Pure data; the server owns the
// authoritative copy and sends it to the client.
class QcInventory
{
public:
    static constexpr int hotbarSize = 9;
    static constexpr int slotCount = 36;

    QcInventory();

    const QcItemStack &slot(int index) const { return m_slots[index]; }
    void setSlot(int index, QcItemStack stack) { m_slots[index] = stack; }
    const QList<QcItemStack> &allSlots() const { return m_slots; }

    int selectedHotbarSlot() const { return m_selected; }
    void setSelectedHotbarSlot(int slot);
    const QcItemStack &selectedStack() const { return m_slots[m_selected]; }

    // Adds items, filling matching stacks first, then empty slots (hotbar first). Damaged items
    // (`damage` > 0) only go into empty slots. Returns the number of items that did not fit.
    int add(QcItemId item, int count, const QcContentRegistry &content, int damage = 0);
    // Removes up to `count` items from a slot; returns how many were removed.
    int removeFromSlot(int index, int count);
    int countOf(QcItemId item) const;
    // The first slot holding ammunition `launcher` takes (decisions E-425, E-427): an item with a
    // projectile kind (QcContentRegistry::finish clears unknown ones; the client gets the cleared
    // names) by the launcher's ammunition list; -1 without one.
    int ammunitionSlot(const QcContentRegistry &content, const QcLauncherDefinition &launcher) const;
    static bool isAmmunition(const QcItemDefinition &item, const QcLauncherDefinition &launcher);
    // Removes `count` items of a type across all stacks; fails without changes if there are fewer.
    [[nodiscard]] bool removeItems(QcItemId item, int count);
    // Moves the stack in `from` onto `to`: merges equal items up to the stack limit, else swaps.
    void move(int from, int to, const QcContentRegistry &content);

    static bool isValidSlot(int index) { return index >= 0 && index < slotCount; }

private:
    QList<QcItemStack> m_slots;
    int m_selected = 0;
};
