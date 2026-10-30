// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcItemStack.h"

#include <QList>

class QcContentRegistry;
struct QcItemDefinition;
struct QcLauncherDefinition;

/*! \brief Player inventory: hotbar slots 0..8 followed by the main storage.

    Pure data; the server owns the authoritative copy and sends it to the client. */
class QcInventory
{
public:
    /*! Slots of the hotbar. */
    static constexpr int hotbarSize = 9;
    /*! Slots in all, the hotbar included. */
    static constexpr int slotCount = 36;

    QcInventory();

    /*! Returns the stack in slot \a index (0 to slotCount - 1). */
    const QcItemStack &slot(int index) const { return m_slots[index]; }
    /*! Puts \a stack into slot \a index (0 to slotCount - 1). */
    void setSlot(int index, const QcItemStack &stack) { m_slots[index] = stack; }
    /*! Returns every slot in order. */
    const QList<QcItemStack> &allSlots() const { return m_slots; }

    /*! Returns the selected hotbar slot. */
    int selectedHotbarSlot() const { return m_selected; }
    /*! Selects the hotbar slot \a slot, clamped to 0..hotbarSize - 1. */
    void setSelectedHotbarSlot(int slot);
    /*! Returns the stack in the selected hotbar slot. */
    const QcItemStack &selectedStack() const { return m_slots[m_selected]; }

    /*! Adds the items of \a adding with their damage and data, filling stacks they stack with
        first (QcItemStack::stacksWith), then empty slots (hotbar first), with the stack sizes of
        \a content. Returns the number of items that did not fit. */
    int add(const QcItemStack &adding, const QcContentRegistry &content);
    /*! Returns the same for \a count plain items (no damage, no data). */
    int add(QcItemId item, int count, const QcContentRegistry &content) { return add(QcItemStack{item, count, 0}, content); }
    /*! Removes up to \a count items from slot \a index; returns how many were removed. */
    int removeFromSlot(int index, int count);
    /*! Returns the number of \a item in all slots. Stacks with contents (a filled backpack) never
        count and are never removed by item: paying or crafting with one would lose what it holds
        (decision E-832). */
    int countOf(QcItemId item) const;
    /*! Returns the first slot holding ammunition of \a content that \a launcher takes (decisions
        E-425, E-427): an item with a projectile kind (QcContentRegistry::finish clears unknown
        ones; the client gets the cleared names) by the launcher's ammunition list; -1 without
        one. */
    int ammunitionSlot(const QcContentRegistry &content, const QcLauncherDefinition &launcher) const;
    /*! Returns whether \a launcher takes \a item as ammunition: an item with a projectile kind
        that its ammunition list names (by name or tag); without a list every such item but
        those thrown by hand. */
    static bool isAmmunition(const QcItemDefinition &item, const QcLauncherDefinition &launcher);
    /*! Removes \a count items of \a item across all stacks without contents. Returns false, with
        no change, if there are fewer. */
    [[nodiscard]] bool removeItems(QcItemId item, int count);
    /*! Moves the stack in \a from onto \a to: merges equal items up to the stack limit of
        \a content, else swaps. */
    void move(int from, int to, const QcContentRegistry &content);

    /*! Returns whether \a index is a slot (0 to slotCount - 1). */
    static bool isValidSlot(int index) { return index >= 0 && index < slotCount; }

private:
    QList<QcItemStack> m_slots;
    int m_selected = 0;
};
