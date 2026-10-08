#pragma once

#include <QString>

// Runtime item id: index into the registry's item table. 0 is "no item" (empty slot).
using QcItemId = quint32;

inline constexpr QcItemId qcNoItem = 0;

struct QcItemDefinition
{
    QString name;
    QString translationKey;
    // Icon texture; block items use the block's top texture unless set.
    QString texture;
    int maxStack = 64;
    // Block placed when the item is used on a block face; empty for non-placeable items.
    QString placesBlock;
    // Tool category (e.g. "pickaxe") and tier; empty for non-tools.
    QString tool;
    int toolLevel = 0;
    // Mining speed multiplier for blocks of this tool category.
    double toolSpeed = 1.0;
    // Uses until a tool breaks; 0 for items without durability.
    int durability = 0;
    // Damage dealt when hitting with this item, in health points.
    double attackDamage = 1.0;
    // Health restored when eaten; 0 for items that are not food.
    double foodHealth = 0.0;
};
