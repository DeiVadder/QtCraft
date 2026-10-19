#pragma once

#include "common/gameplay/QcItemStack.h"

#include <QList>

#include <array>
#include <cstddef>

class QcContentRegistry;

// What a player or an armour stand wears in the four armour slots (QcEquipmentSlot, decisions
// E-232, E-234): one stack each. The protocol carries it for the own player in InventoryState and
// for players and stands others see in EntitySpawn.
struct QcEquipment
{
    static constexpr std::size_t slotCount = 4;

    std::array<QcItemStack, slotCount> stacks{};

    const QcItemStack &at(QcEquipmentSlot slot) const { return stacks[std::size_t(slot)]; }
    QcItemStack &at(QcEquipmentSlot slot) { return stacks[std::size_t(slot)]; }
    bool isEmpty() const
    {
        for (const QcItemStack &stack : stacks) {
            if (!stack.isEmpty()) {
                return false;
            }
        }
        return true;
    }

    // Only the stacks of items the content knows; the others empty. The
    // client checks what the server sends before anything is drawn from it.
    QcEquipment knownTo(const QcContentRegistry &content) const;
    // Wears every worn piece with durability by a hit of `damage` (QcArmour::wearOf); broken pieces
    // are removed. Returns their slots.
    [[nodiscard]] QList<QcEquipmentSlot> wear(const QcContentRegistry &content, double damage);

    bool operator==(const QcEquipment &) const = default;
};
