#include "common/gameplay/QcArmour.h"

#include "common/content/registry/QcContentRegistry.h"

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

bool fits(const QcContentRegistry &content, const QcItemStack &stack, QcEquipmentSlot slot)
{
    const QcArmourDefinition *armour = stack.isEmpty() ? nullptr : of(content, stack.item);
    return armour && armour->slot == slot && stack.count == 1;
}

[[nodiscard]] std::optional<QcItemId> placeholder(const QcContentRegistry &content, QcEquipmentSlot slot)
{
    for (const QcItemDefinition &item : content.items()) {
        if (item.armour && item.armour->slot == slot) {
            return content.itemId(item.name);
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<QcItemId> pointsIcon(const QcContentRegistry &content)
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
    return std::max(1, static_cast<int>(std::floor(damage / damagePerWear)));
}

} // namespace QcArmour
