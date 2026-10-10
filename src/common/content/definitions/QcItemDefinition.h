#pragma once

#include "QcArmourDefinition.h"

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
    // Translation key of the handbook's text about it ("<translation>.description" unless set); a
    // missing text leaves only the facts. Server side only.
    QString descriptionKey;
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
    // Used on a liquid source, takes it and becomes the liquid's bucket item (empty bucket).
    bool scoops = false;
    // Used on a liquid, fills from it and becomes the liquid's bottle item, leaving the liquid
    // (glass bottle, decision E-211).
    bool bottles = false;
    // What the item turns into once it placed its block (a filled bucket becomes empty) or was
    // eaten (soup leaves its bowl).
    QString emptiesTo;
    // Worn in an equipment slot (decision E-234); items with armour stack to 1.
    std::optional<QcArmourDefinition> armour;
    // Used on the top of a block, places an armour stand that drops this item again (decision E-238).
    bool placesStand = false;
};
