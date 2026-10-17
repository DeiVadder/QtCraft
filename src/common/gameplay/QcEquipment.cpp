#include "common/gameplay/QcEquipment.h"

#include "common/content/registry/QcContentRegistry.h"

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
