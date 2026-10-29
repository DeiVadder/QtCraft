// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

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
    void setSlot(int index, const QcItemStack &stack) { m_slots[index] = stack; }
    const QList<QcItemStack> &allSlots() const { return m_slots; }

    int selectedHotbarSlot() const { return m_selected; }
    void setSelectedHotbarSlot(int slot);
    const QcItemStack &selectedStack() const { return m_slots[m_selected]; }

    // Adds the items of `adding` with their damage and data, filling stacks they stack with first
    // (QcItemStack::stacksWith), then empty slots (hotbar first). Returns the number of items that
    // did not fit.
    int add(const QcItemStack &adding, const QcContentRegistry &content);
    // The same for `count` plain items (no damage, no data).
    int add(QcItemId item, int count, const QcContentRegistry &content) { return add(QcItemStack{item, count, 0}, content); }
    // Removes up to `count` items from a slot; returns how many were removed.
    int removeFromSlot(int index, int count);
    // Stacks with contents (a filled backpack) never count and are never removed by item: paying
    // or crafting with one would lose what it holds (decision E-832).
    int countOf(QcItemId item) const;
    // The first slot holding ammunition `launcher` takes (decisions E-425, E-427): an item with a
    // projectile kind (QcContentRegistry::finish clears unknown ones; the client gets the cleared
    // names) by the launcher's ammunition list; -1 without one.
    int ammunitionSlot(const QcContentRegistry &content, const QcLauncherDefinition &launcher) const;
    static bool isAmmunition(const QcItemDefinition &item, const QcLauncherDefinition &launcher);
    // Removes `count` items of a type across all stacks without contents; fails without changes if
    // there are fewer.
    [[nodiscard]] bool removeItems(QcItemId item, int count);
    // Moves the stack in `from` onto `to`: merges equal items up to the stack limit, else swaps.
    void move(int from, int to, const QcContentRegistry &content);

    static bool isValidSlot(int index) { return index >= 0 && index < slotCount; }

private:
    QList<QcItemStack> m_slots;
    int m_selected = 0;
};
