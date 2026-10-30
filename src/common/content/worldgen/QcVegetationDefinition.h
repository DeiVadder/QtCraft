// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

/*! \brief One plant of a table and how often it is chosen against the others.

    \c {block} is the qualified plant block (an entry of \c {"plants"} or its \c {"block"}),
    \c {weight} its relative weight (\c {"weight"}, 1 to QcVegetationLayer::maxWeight, 1 for a
    plain name). \qcfields */
struct QcVegetationPlant
{
    QString block;
    int weight = 1;
};

/*! \brief Patches the generator lays out of one plant of the table (flower patches): a patch
    cell of patchCell × patchCell blocks holds one with \c {chance}, centred at a random spot of
    the cell; it tries \c {tries} columns at most \c {radius} blocks from its centre on each axis
    and plants one kind, chosen by weight, on every free one. Read from the \c {"patches"}
    object. */
struct QcVegetationPatches
{
    /*! Side in blocks of a patch cell, aligned with the chunks. */
    static constexpr int patchCell = 16;
    /*! Largest accepted \c {radius}. */
    static constexpr int maxRadius = 8;
    /*! Chance that a patch cell holds a patch (\c {"chance"}, 0 to 1). */
    double chance = 0.0;
    /*! Columns tried per patch (\c {"tries"}, 1 to 128). */
    int tries = 32;
    /*! Largest distance of a tried column from the centre on each axis (\c {"radius"}, 1 to
        maxRadius). */
    int radius = 5;
};

/*! \brief One layer of vegetation: the plants, their density per surface column (scattered by
    the generator), the size of the areas one kind grows in (\c {patchSize} blocks, 0: mixed
    column by column), and the patches. */
struct QcVegetationLayer
{
    /*! Most entries accepted in \c {"plants"}. */
    static constexpr int maxPlants = 32;
    /*! Largest accepted plant weight. */
    static constexpr int maxWeight = 1000;
    /*! Density of the scattered plants per surface column (\c {"density"}, 0 to 1). */
    double density = 0.0;
    /*! Size in blocks of the areas one kind grows in (\c {"patchSize"}, 0 to 512); 0: mixed
        column by column. */
    int patchSize = 0;
    /*! Patches of a single plant kind (\c {"patches"}). */
    QcVegetationPatches patches;
    /*! The weighted plant table (\c {"plants"}, 1 to maxPlants entries). */
    QList<QcVegetationPlant> plants;
};

/*! \brief The grass and flowers of a biome (decision E-814): two weighted tables of plant blocks.

    Used by the world generator for the surface vegetation and by bone meal on a block that
    \c {growsVegetation} (grass) for the plants it spreads. Server side only. */
struct QcVegetationDefinition
{
    /*! Short grass and ferns: most of what bone meal grows. */
    QcVegetationLayer grass;
    /*! Flowers: one plant in eight that bone meal grows. */
    QcVegetationLayer flowers;
};
