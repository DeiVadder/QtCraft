#pragma once

#include <QString>

// Turns one input item into a result inside a machine of the same category (furnace: "smelting");
// the stonecutter's recipes (decision E-784) have the category "stonecutting" and their own list.
struct QcSmeltingRecipe
{
    static constexpr auto defaultCategory = "smelting";
    static constexpr auto stonecuttingCategory = "stonecutting";
    static constexpr double minSeconds = 0.05;
    static constexpr double maxSeconds = 3600.0;

    QString name;
    QString category = QString::fromLatin1(defaultCategory);
    QString inputItem;
    QString resultItem;
    int resultCount = 1;
    // Burning time per item at machine speed 1.
    double seconds = 10.0;
    // Experience points per item smelted, gathered in the machine until a player takes the output
    // (decision E-398). Server side only.
    double experience = 0.0;
    static constexpr double maxExperience = 100.0;
    // What is smelted ("food", "metal"; empty: anything else). Machines with `kinds` take only
    // recipes of those kinds: the smoker food, the blast furnace metal (decision E-770).
    QString kind = {}; // NOLINT(readability-redundant-member-init): keeps aggregate initialisations warning-free
};
