#pragma once

#include <QString>

// Turns one input item into a result inside a machine of the same category (furnace: "smelting").
struct QcSmeltingRecipe
{
    static constexpr auto defaultCategory = "smelting";
    static constexpr double minSeconds = 0.05;
    static constexpr double maxSeconds = 3600.0;

    QString name;
    QString category = QString::fromLatin1(defaultCategory);
    QString inputItem;
    QString resultItem;
    int resultCount = 1;
    // Burning time per item at machine speed 1.
    double seconds = 10.0;
};
