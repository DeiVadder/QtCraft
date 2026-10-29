// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/gameplay/QcEnchanting.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcEquipment.h"

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;

namespace QcEnchanting {

QStringList kindsOf(const QcItemDefinition &item)
{
    QStringList kinds;
    if (!item.tool.isEmpty()) {
        kinds.append(item.tool);
    }
    static constexpr std::array<QLatin1StringView, 4> diggers{QLatin1StringView("pickaxe"), QLatin1StringView("axe"),
                                                               QLatin1StringView("shovel"), QLatin1StringView("hoe")};
    if (std::ranges::find(diggers, item.tool) != diggers.end()) {
        kinds.append(u"digger"_s);
    }
    // Swords have no tool category but deal more than a hand.
    if (item.tool.isEmpty() && item.durability > 0 && item.attackDamage > 1.0 && !item.armour && !item.launcher) {
        kinds.append(u"weapon"_s);
    }
    if (item.armour) {
        static constexpr std::array<QLatin1StringView, 4> slotKinds{QLatin1StringView("armour_head"), QLatin1StringView("armour_chest"),
                                                                    QLatin1StringView("armour_legs"), QLatin1StringView("armour_feet")};
        kinds.append(u"armour"_s);
        const auto slot = static_cast<std::size_t>(item.armour->slot);
        if (slot < slotKinds.size()) {
            kinds.append(QString(slotKinds[slot]));
        }
    }
    if (item.launcher) {
        kinds.append(u"launcher"_s);
    }
    if (item.durability > 0) {
        kinds.append(u"durable"_s);
    }
    return kinds;
}

bool appliesTo(const QcEnchantmentDefinition &enchantment, const QcItemDefinition &item)
{
    // An enchanted book takes every enchantment (decision E-873).
    if (item.storesEnchantments) {
        return true;
    }
    const QStringList kinds = kindsOf(item);
    return std::ranges::any_of(enchantment.items, [&kinds](const QString &kind) { return kinds.contains(kind); });
}

bool conflict(const QcEnchantmentDefinition &a, const QcEnchantmentDefinition &b)
{
    return a.name == b.name || a.conflicts.contains(b.name) || b.conflicts.contains(a.name);
}

int levelOf(const QcContentRegistry &content, const QcItemStack &stack, QcEnchantmentEffect effect)
{
    // A book's enchantments are stored, not used (decision E-873).
    if (stack.enchantments().isEmpty() || !content.isValidItem(stack.item) || content.item(stack.item).storesEnchantments) {
        return 0;
    }
    int level = 0;
    for (const QcEnchantment &enchantment : stack.enchantments()) {
        const QcEnchantmentDefinition *definition = content.enchantment(enchantment.name);
        if (definition && definition->effect == effect) {
            level = std::max(level, enchantment.level);
        }
    }
    return level;
}

double speedBonus(int level)
{
    return level > 0 ? static_cast<double>(level * level + 1) : 0.0;
}

bool keepsDurability(int level, double roll, bool armour)
{
    if (level <= 0) {
        return false;
    }
    const double keep = static_cast<double>(level) / static_cast<double>(level + 1);
    return roll < (armour ? 0.4 * keep : keep);
}

double damageBonus(int level)
{
    return level > 0 ? 0.5 * level + 0.5 : 0.0;
}

int protectionPoints(const QcContentRegistry &content, const QcEquipment &equipment, bool fall)
{
    int points = 0;
    for (const QcItemStack &stack : equipment.stacks) {
        points += levelOf(content, stack, QcEnchantmentEffect::Protection);
        if (fall) {
            points += 3 * levelOf(content, stack, QcEnchantmentEffect::FeatherFalling);
        }
    }
    return std::min(points, maxProtectionPoints);
}

double reducedDamage(double amount, int points)
{
    return amount * (1.0 - 0.04 * std::clamp(points, 0, maxProtectionPoints));
}

} // namespace QcEnchanting
