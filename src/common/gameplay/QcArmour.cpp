// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

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

namespace {

// The sum of `value` over the armour worn in `equipment`.
double sumOf(const QcContentRegistry &content, const QcEquipment &equipment, double QcArmourDefinition::*value)
{
    double sum = 0.0;
    for (const QcItemStack &stack : equipment.stacks) {
        if (const QcArmourDefinition *armour = stack.isEmpty() ? nullptr : of(content, stack.item)) {
            sum += armour->*value;
        }
    }
    return sum;
}

} // namespace

double toughness(const QcContentRegistry &content, const QcEquipment &equipment)
{
    return sumOf(content, equipment, &QcArmourDefinition::toughness);
}

double knockbackResistance(const QcContentRegistry &content, const QcEquipment &equipment)
{
    return std::min(1.0, sumOf(content, equipment, &QcArmourDefinition::knockbackResistance));
}

double reducedDamage(double damage, int points, double toughness)
{
    if (points <= 0 || damage <= 0.0) {
        return std::max(0.0, damage);
    }
    const double pierced = static_cast<double>(points) - damage / (2.0 + std::max(0.0, toughness) / 4.0);
    const double effective = std::clamp(pierced, static_cast<double>(points) * 0.2, static_cast<double>(maxEffectivePoints));
    return damage * (1.0 - reductionPerPoint * effective);
}

int wearOf(double damage)
{
    return std::max(1, static_cast<int>(std::floor(damage / damagePerWear)));
}

} // namespace QcArmour
