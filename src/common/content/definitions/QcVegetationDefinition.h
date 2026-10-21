#pragma once

#include <QList>
#include <QString>

// The grass and flowers of a biome (decision E-814): two weighted tables of plant blocks, used by
// the world generator for the surface vegetation and by bone meal on a block that `growsVegetation`
// (grass) for the plants it spreads. Server side only.

// One plant of a table and how often it is chosen against the others.
struct QcVegetationPlant
{
    QString block;
    int weight = 1;
};

// Patches the generator lays out of one plant of the table (flower patches): a patch cell of
// patchCell × patchCell blocks holds one with `chance`, centred at a random spot of the cell; it
// tries `tries` columns at most `radius` blocks from its centre on each axis and plants one kind,
// chosen by weight, on every free one.
struct QcVegetationPatches
{
    static constexpr int patchCell = 16;
    static constexpr int maxRadius = 8;
    double chance = 0.0;
    int tries = 32;
    int radius = 5;
};

// One layer of vegetation: the plants, their density per surface column (scattered by the
// generator), the size of the areas one kind grows in (`patchSize` blocks, 0: mixed column by
// column), and the patches.
struct QcVegetationLayer
{
    static constexpr int maxPlants = 32;
    static constexpr int maxWeight = 1000;
    double density = 0.0;
    int patchSize = 0;
    QcVegetationPatches patches;
    QList<QcVegetationPlant> plants;
};

struct QcVegetationDefinition
{
    // Short grass and ferns: most of what bone meal grows.
    QcVegetationLayer grass;
    // Flowers: one plant in eight that bone meal grows.
    QcVegetationLayer flowers;
};
