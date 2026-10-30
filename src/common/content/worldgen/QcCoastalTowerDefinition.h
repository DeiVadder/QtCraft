// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

/*! \brief Coastal towers of the terrain generator (\c {"coastalTowers"} in worldgen/terrain.json,
    decision E-605): a tower from structure data on land at or a little above sea level with ocean
    near it, standing on a foundation down to the ground.

    At most one per square region of \c {regionSize} blocks, with \c {chance}; the tower lies
    wholly inside its region. Server side only. */
struct QcCoastalTowerSettings
{
    /*! \brief One entry of \c {"structures"}: a tower structure and its weight.

        \c {structure} is the qualified structure name (key \c {"structure"}); \c {weight} is
        its relative weight in the pick (key \c {"weight"}, at least 1). \qcfields */
    struct Structure
    {
        QString structure;
        int weight = 1;
    };

    /*! No "coastalTowers" object, or \c {"enabled": false}: none. */
    bool enabled = false;
    /*! Side of the square region in blocks that holds at most one tower (\c {"regionSize"},
        48 to maxRegionSize). */
    int regionSize = 160;
    /*! Chance that a region gets a tower (\c {"chance"}, 0 to 1). */
    double chance = 0.6;
    /*! Towers of neighbouring regions stand at least this many blocks apart (centre to centre);
        of two that would come closer, the one of the region with the lower priority is left out
        (decision E-857). 0: no limit. */
    int minDistance = 0;
    /*! Sites checked fully per region (land near the sea found on a grid of \c {gridStep}
        blocks). */
    int attempts = 16;
    /*! Spacing in blocks of the grid on which land near the sea is searched (\c {"gridStep"}). */
    int gridStep = 6;
    /*! The ground's top under the tower lies from sea level up to \c {maxRise} blocks above it,
        and its highest and lowest columns differ by at most \c {maxSlope}. */
    int maxRise = 5;
    /*! Largest height difference in blocks between the highest and lowest ground column under
        the tower (\c {"maxSlope"}). */
    int maxSlope = 3;
    /*! Ocean within \c {coastDistance} blocks of the tower's edge: a column of the sea (land
        factor below one half) whose ground lies at least \c {oceanDepth} blocks below the
        water's surface. */
    int coastDistance = 12;
    /*! Depth in blocks of the ground below the water's surface for a column to count as ocean
        (\c {"oceanDepth"}). */
    int oceanDepth = 3;
    /*! Blocks kept free of villages and mine shaft entrances around the tower. */
    int clearance = 8;
    /*! The block under the tower down to the ground, at most \c {maxFoundation} blocks deep. */
    QString foundation;
    /*! Deepest foundation in blocks below the tower's floor (\c {"maxFoundation"}). */
    int maxFoundation = 12;
    /*! Tower structures picked by weight (\c {"structures"}); at least one is required. */
    QList<Structure> structures;

    /*! Largest accepted \c {regionSize}. */
    static constexpr int maxRegionSize = 512;
    /*! Largest accepted \c {coastDistance} and \c {clearance}. */
    static constexpr int maxDistance = 48;
};
