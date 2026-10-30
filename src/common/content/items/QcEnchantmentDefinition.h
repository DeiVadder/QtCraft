// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>
#include <QtGlobal>

/*! \brief What an enchantment does (decision E-864): the game knows these effects; an
    enchantment's data names one of them (\c {"effect": "efficiency"}), so a mod can add an
    enchantment with its own name, levels and items for a known effect, but no new kind of
    effect. */
enum class QcEnchantmentEffect : quint8 {
    /*! Mining speed: the tool's speed gains level² + 1 on blocks of its category (QcMining). */
    Efficiency,
    /*! Durability: each use wears the item only with chance 1 / (level + 1); armour with
        0.6 + 0.4 / (level + 1) (QcEnchanting::keepsDurability). */
    Unbreaking,
    /*! Melee damage: + 0.5 × level + 0.5 health points per hit. */
    Sharpness,
    /*! Protection points against every damage the armour takes off, and falls: level per piece. */
    Protection,
    /*! Protection points against falls only: 3 × level per piece. */
    FeatherFalling,
};

/*! \brief An enchantment (enchantments/<name>.json, decision E-864), as in the reference game.

    It names the items it applies to, how often the enchanting table picks it, the enchantments it
    excludes, its effect and the enchanting power each level needs: from \c {minPower} +
    \c {powerPerLevel} × (level - 1) up to that plus \c {powerSpread}.

    The content sync sends all of it: the client needs the effect (mining speed it predicts) and
    the name for the tooltip. */
struct QcEnchantmentDefinition
{
    /*! The highest level an enchantment may have in its data and on an item. */
    static constexpr int maxLevelLimit = 10;

    /*! Qualified name (\c {"name"}, required, at most QcItemData::maxEnchantmentName
        characters). */
    QString name;
    /*! Translation key of its display name (\c {"translation"}); defaults to
        \c {enchantment.<namespace>.<name>}. */
    QString translationKey;
    /*! The effect it has (\c {"effect"}, required, a name known to
        QcEnchantmentParser::effectOf()). */
    QcEnchantmentEffect effect = QcEnchantmentEffect::Unbreaking;
    /*! Highest level it can have (\c {"maxLevel"}, 1 to maxLevelLimit, default 1). */
    int maxLevel = 1;
    /*! Relative chance against the other candidates of an offer (1–1000). */
    int weight = 10;
    /*! Item kinds it applies to (QcEnchanting::kindsOf), 1 to 16 of: a tool category ("pickaxe",
        "axe", "shovel", "hoe", "shears"), "digger" (pickaxe, axe, shovel, hoe), "weapon",
        "armour", "armour_head" … "armour_feet", "launcher" or "durable". */
    QStringList items;
    /*! Enchantments that never come together with it (both ways). */
    QStringList conflicts;
    /*! Enchanting power level 1 needs at least (\c {"minPower"}, 1 to 100). */
    int minPower = 1;
    /*! Power each further level adds to the lowest power (\c {"powerPerLevel"}, 0 to 100). */
    int powerPerLevel = 10;
    /*! Width of each level's power range above its lowest power (\c {"powerSpread"}, 0 to
        100). */
    int powerSpread = 50;

    /*! Returns the lowest enchanting power at which \a level is offered. */
    int lowestPower(int level) const { return minPower + powerPerLevel * (level - 1); }
    /*! Returns the highest enchanting power at which \a level is offered: lowestPower() plus
        \c {powerSpread}. */
    int highestPower(int level) const { return lowestPower(level) + powerSpread; }
    bool operator==(const QcEnchantmentDefinition &) const = default;
};
