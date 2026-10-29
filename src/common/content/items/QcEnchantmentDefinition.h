// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

// What an enchantment does (decision E-864): the game knows these effects; an enchantment's data
// names one of them (`"effect": "efficiency"`), so a mod can add an enchantment with its own name,
// levels and items for a known effect, but no new kind of effect.
enum class QcEnchantmentEffect : quint8 {
    // Mining speed: the tool's speed gains level² + 1 on blocks of its category (QcMining).
    Efficiency,
    // Durability: each use wears the item only with chance 1 / (level + 1); armour with
    // 0.6 + 0.4 / (level + 1) (QcEnchanting::keepsDurability).
    Unbreaking,
    // Melee damage: + 0.5 × level + 0.5 health points per hit.
    Sharpness,
    // Protection points against every damage the armour takes off, and falls: level per piece.
    Protection,
    // Protection points against falls only: 3 × level per piece.
    FeatherFalling,
};

// An enchantment (enchantments/<name>.json, decision E-864), as in the reference game: the items
// it applies to, how often the enchanting table picks it, the enchantments it excludes, its effect
// and the enchanting power each level needs (`minPower` + `powerPerLevel` × (level - 1) up to that
// plus `powerSpread`). The content sync sends all of it: the client needs the effect (mining speed
// it predicts) and the name for the tooltip.
struct QcEnchantmentDefinition
{
    // The highest level an enchantment may have in its data and on an item.
    static constexpr int maxLevelLimit = 10;

    QString name;
    QString translationKey;
    QcEnchantmentEffect effect = QcEnchantmentEffect::Unbreaking;
    int maxLevel = 1;
    // Relative chance against the other candidates of an offer (1–1000).
    int weight = 10;
    // Item kinds it applies to (QcEnchanting::kindsOf): a tool category such as "pickaxe", "digger"
    // (pickaxe, axe, shovel, hoe), "weapon", "armour", "armour_head" … "armour_feet", "launcher",
    // "fishing" or "durable".
    QStringList items;
    // Enchantments that never come together with it (both ways).
    QStringList conflicts;
    int minPower = 1;
    int powerPerLevel = 10;
    int powerSpread = 50;

    // The enchanting power range in which `level` is offered.
    int lowestPower(int level) const { return minPower + powerPerLevel * (level - 1); }
    int highestPower(int level) const { return lowestPower(level) + powerSpread; }
    bool operator==(const QcEnchantmentDefinition &) const = default;
};
