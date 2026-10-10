#pragma once

#include "QcItemStack.h"

#include <QList>

class QcContentRegistry;

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
    // Removes `count` items of a type across all stacks; fails without changes if there are fewer.
    bool removeItems(QcItemId item, int count);
    // Moves the stack in `from` onto `to`: merges equal items up to the stack limit, else swaps.
    void move(int from, int to, const QcContentRegistry &content);

    // Moves up to `count` items (0 = the whole stack) between two slots of any slot list: into an
    // empty or matching slot, or swaps whole stacks otherwise. Returns false for invalid indices.
    static bool moveBetween(QList<QcItemStack> &stacks, int from, int to, int count, const QcContentRegistry &content);

    // Moves as much of the stack in `from` as fits into `targets`: first onto equal stacks (in the
    // order of `targets`), then into the first empty ones. Returns the number of items moved.
    static int moveInto(QList<QcItemStack> &stacks, int from, const QList<int> &targets, const QcContentRegistry &content);

    static bool isValidSlot(int index) { return index >= 0 && index < slotCount; }

private:
    QList<QcItemStack> m_slots;
    int m_selected = 0;
};
