#pragma once

#include <QString>
#include <QStringList>

// Runtime item id: index into the registry's item table. 0 is "no item" (empty slot).
using QcItemId = quint32;

inline constexpr QcItemId qcNoItem = 0;

struct QcItemDefinition
{
    // Largest allowed maxStack.
    static constexpr int maxStackLimit = 999;
    // Food points of a full player.
    static constexpr int maxFood = 20;

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
    // Food points restored when eaten (a full player has 20); 0 for items that are not food.
    int food = 0;
    // Seconds the item burns as fuel in a machine; 0 for items that are no fuel.
    double fuelSeconds = 0.0;
    // Groups the item belongs to ("qtcraft:planks"); recipes accept any member of "#group".
    QStringList tags;
};
