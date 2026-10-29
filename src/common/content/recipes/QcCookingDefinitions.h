// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

// Whether a smelting recipe of `kind` (QcSmeltingRecipe::kind, empty: none) works in a machine or
// campfire that takes `kinds` (empty: every kind; decision E-770).
inline bool qcAcceptsRecipeKind(const QStringList &kinds, const QString &kind)
{
    return kinds.isEmpty() || kinds.contains(kind);
}

// A block eaten bite by bite where it stands (cake, decision E-773): using it eats one bite of `food`
// points; its state `bites` counts the bites taken, the bite after its last value removes the block.
struct QcBitesDefinition
{
    int food = 0;

    bool isEdible() const { return food > 0; }
};

// A campfire (decision E-772). A lit one (`slotCount` above 0) takes up to `slotCount` items with the use
// button that the smelting recipes of `category` and `kinds` turn into their results in `seconds`
// each (the recipe's own time does not count); the results drop beside it. `extinguishesInto` is the
// block it becomes when put out (a shovel, a water bucket), `lightsInto` the block an unlit one
// becomes when lit (flint and steel). Only `slotCount` reaches the client.
struct QcCampfireDefinition
{
    static constexpr int maxSlots = 4;
    static constexpr double maxSeconds = 3600.0;

    int slotCount = 0;
    double seconds = 30.0;
    QString category;
    QStringList kinds;
    QString extinguishesInto;
    QString lightsInto;

    bool cooks() const { return slotCount > 0; }
    bool isCampfire() const { return cooks() || !lightsInto.isEmpty(); }
};
