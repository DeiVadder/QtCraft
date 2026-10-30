// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/gameplay/QcEquipment.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcArmour.h"

QcEquipment QcEquipment::knownTo(const QcContentRegistry &content) const
{
    QcEquipment known;
    for (std::size_t slot = 0; slot < slotCount; ++slot) {
        const QcItemStack &stack = stacks[slot];
        if (!stack.isEmpty() && content.isValidItem(stack.item)) {
            known.stacks[slot] = stack;
        }
    }
    return known;
}

QList<QcEquipmentSlot> QcEquipment::wear(const QcContentRegistry &content, double damage, const std::function<bool(const QcItemStack &)> &keeps)
{
    QList<QcEquipmentSlot> broken;
    const int loss = QcArmour::wearOf(damage);
    for (std::size_t i = 0; i < slotCount; ++i) {
        QcItemStack &stack = stacks[i];
        if (stack.isEmpty() || !content.isValidItem(stack.item) || content.item(stack.item).durability <= 0) {
            continue;
        }
        if (keeps && keeps(stack)) {
            continue;
        }

        stack.damage += loss;
        if (stack.damage >= content.item(stack.item).durability) {
            stack = {};
            broken.append(static_cast<QcEquipmentSlot>(i));
        }
    }
    return broken;
}
