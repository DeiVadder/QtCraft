#pragma once

#include "QcItemStack.h"

#include <array>
#include <cstddef>

class QcContentRegistry;

// The four armour slots of a player (decision E-232). The protocol carries them from stage 22 on,
// for the own player in InventoryState and for others in EntitySpawn, so stage 23 (armour) and
// multiplayer only fill them; until then they stay empty.
enum class QcEquipmentSlot : quint8 { Head, Chest, Legs, Feet };

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

    bool operator==(const QcEquipment &) const = default;
};
