// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/gameplay/QcItemStack.h"

#include <QList>

#include <array>
#include <cstddef>
#include <functional>

class QcContentRegistry;

/*! \brief What a player or an armour stand wears in the four armour slots (QcEquipmentSlot,
    decisions E-232, E-234): one stack each.

    The protocol carries it for the own player in InventoryState and for players and stands
    others see in EntitySpawn. */
struct QcEquipment
{
    /*! The number of armour slots. */
    static constexpr std::size_t slotCount = 4;

    /*! The stack of each slot, in the order of QcEquipmentSlot. */
    std::array<QcItemStack, slotCount> stacks{};

    /*! Returns the stack in \a slot. */
    const QcItemStack &at(QcEquipmentSlot slot) const { return stacks[static_cast<std::size_t>(slot)]; }
    /*! Returns the stack in \a slot to change it. */
    QcItemStack &at(QcEquipmentSlot slot) { return stacks[static_cast<std::size_t>(slot)]; }
    /*! Returns whether every slot is empty. */
    bool isEmpty() const
    {
        for (const QcItemStack &stack : stacks) {
            if (!stack.isEmpty()) {
                return false;
            }
        }
        return true;
    }

    /*! Returns only the stacks of items \a content knows; the others empty. The client checks
        what the server sends before anything is drawn from it. */
    QcEquipment knownTo(const QcContentRegistry &content) const;
    /*! Wears every worn piece with durability (as \a content defines it) by a hit of \a damage
        (QcArmour::wearOf); broken pieces are removed. Returns their slots. \a keeps tells for a
        piece whether it does not wear this time (unbreaking, decision E-864). */
    [[nodiscard]] QList<QcEquipmentSlot> wear(const QcContentRegistry &content, double damage,
                                              const std::function<bool(const QcItemStack &)> &keeps = {});

    bool operator==(const QcEquipment &) const = default;
};
