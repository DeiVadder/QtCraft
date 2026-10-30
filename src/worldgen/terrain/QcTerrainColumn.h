// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QtGlobal>

/*! \brief Shape and biome of one terrain column, before caves and features. */
struct QcTerrainColumn
{
    /*! Height of the terrain surface: the top solid block is at height - 1. */
    int height = 0;
    /*! The height before rounding, as column() computed it; where something moved the column
        since (it no longer rounds to \c {height}), \c {height} counts. */
    double exactHeight = 0.0;
    /*! Index of the column's biome in QcTerrainDefinitions::biomes. */
    int biome = 0;
    /*! Marks a column near a biome border whose ground biome is not looked up yet
        (QcTerrainGenerator::withGround() does). */
    static constexpr int unresolvedGround = -2;
    /*! The biome whose ground blocks the column takes (decision E-858): near a biome border the
        one a few jittered blocks away, so the ground mixes; -1 for \c {biome} itself,
        \c {unresolvedGround} while not looked up. */
    int groundBiome = -1;
    /*! The biome of the climate alone, before a river channel takes its river biome. */
    int landBiome = 0;
    /*! The climate's temperature at the column in [0, 1]: the top water layer freezes by it. */
    double temperature = 0.5;
    /*! 0 = ocean, 1 = land; values in between are coasts. */
    double landFactor = 1.0;
    /*! Largest height difference to the four neighbouring columns; set where it is needed
        (generate(), surfaceBlockAt()), else 0. */
    int slope = 0;
    /*! Overhang amplitude in blocks, blended across biome borders (0: plain column). */
    double overhang = 0.0;
    /*! Dry ground at the water's edge of a river (decision E-840): the river biome's beach in
        patches. The water of a river channel has the river biome as \c {biome}. */
    bool riverBank = false;
    /*! The river near the column (decision E-840): blocks from its middle line (about; 1e9 far
        from rivers and where the ground lies too deep under water for one) and half the width of
        its water there (0 where none runs), as QcTerrainGenerator::riverAt() gives them.
        \qcshared */
    double riverDistance = 1.0e9;
    double riverHalfWidth = 0.0;

    /*! Returns the biome whose ground blocks the column takes: \c {groundBiome}, or \c {biome}
        for -1. Only for a looked-up column (never \c {unresolvedGround}). */
    int ground() const
    {
        Q_ASSERT(groundBiome != unresolvedGround);
        return groundBiome >= 0 ? groundBiome : biome;
    }
};
