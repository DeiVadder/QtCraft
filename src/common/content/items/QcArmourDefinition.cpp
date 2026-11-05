// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/items/QcArmourDefinition.h"

QString qcEquipmentSlotName(QcEquipmentSlot slot)
{
    switch (slot) {
    case QcEquipmentSlot::Head:
        return QStringLiteral("head");
    case QcEquipmentSlot::Chest:
        return QStringLiteral("chest");
    case QcEquipmentSlot::Legs:
        return QStringLiteral("legs");
    case QcEquipmentSlot::Feet:
        break;
    }
    return QStringLiteral("feet");
}

[[nodiscard]] std::optional<QcEquipmentSlot> qcEquipmentSlotFromName(const QString &name)
{
    for (const QcEquipmentSlot slot : {QcEquipmentSlot::Head, QcEquipmentSlot::Chest, QcEquipmentSlot::Legs, QcEquipmentSlot::Feet}) {
        if (qcEquipmentSlotName(slot) == name) {
            return slot;
        }
    }
    return std::nullopt;
}
