#include "QcArmour.h"

#include "QcContentRegistry.h"

#include <algorithm>
#include <cmath>

namespace QcArmour {

const QcArmourDefinition *of(const QcContentRegistry &content, QcItemId item)
{
    if (!content.isValidItem(item)) {
        return nullptr;
    }
    const std::optional<QcArmourDefinition> &armour = content.item(item).armour;
    return armour ? &*armour : nullptr;
}

bool fits(const QcContentRegistry &content, QcItemStack stack, QcEquipmentSlot slot)
{
    const QcArmourDefinition *armour = stack.isEmpty() ? nullptr : of(content, stack.item);
    return armour && armour->slot == slot && stack.count == 1;
}

std::optional<QcItemId> placeholder(const QcContentRegistry &content, QcEquipmentSlot slot)
{
    for (const QcItemDefinition &item : content.items()) {
        if (item.armour && item.armour->slot == slot) {
            return content.itemId(item.name);
        }
    }
    return std::nullopt;
}

std::optional<QcItemId> pointsIcon(const QcContentRegistry &content)
{
    const QcItemDefinition *best = nullptr;
    int bestPoints = -1;
    for (const QcItemDefinition &item : content.items()) {
        if (item.armour && item.armour->slot == QcEquipmentSlot::Chest && item.armour->points > bestPoints) {
            best = &item;
            bestPoints = item.armour->points;
        }
    }
    return best ? content.itemId(best->name) : std::nullopt;
}

int points(const QcContentRegistry &content, const QcEquipment &equipment)
{
    int sum = 0;
    for (const QcItemStack &stack : equipment.stacks) {
        if (const QcArmourDefinition *armour = stack.isEmpty() ? nullptr : of(content, stack.item)) {
            sum += armour->points;
        }
    }
    return sum;
}

double reducedDamage(double damage, int points)
{
    const int effective = std::clamp(points, 0, maxEffectivePoints);
    return damage * (1.0 - reductionPerPoint * effective);
}

int wearOf(double damage)
{
    return std::max(1, int(std::floor(damage / damagePerWear)));
}

QList<QcEquipmentSlot> wear(const QcContentRegistry &content, QcEquipment &equipment, double damage)
{
    QList<QcEquipmentSlot> broken;
    const int loss = wearOf(damage);
    for (std::size_t i = 0; i < QcEquipment::slotCount; ++i) {
        QcItemStack &stack = equipment.stacks[i];
        if (stack.isEmpty() || !content.isValidItem(stack.item) || content.item(stack.item).durability <= 0) {
            continue;
        }

        stack.damage += loss;
        if (stack.damage >= content.item(stack.item).durability) {
            stack = {};
            broken.append(QcEquipmentSlot(i));
        }
    }
    return broken;
}

} // namespace QcArmour
