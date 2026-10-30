// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/terrain/QcNoise.h"
#include "worldgen/villages/QcVillagePlan.h"
#include "worldgen/villages/QcWetlandPattern.h"

#include <optional>

class QcTerrainGenerator;

/*! \brief The ground a village over water shapes around itself (decision E-407, E-660,
    QcVillagePlan::Wetland): within the wetland's margin of its pieces and boardwalks, frayed by
    noise, the ground lies at the water surface, deep water filled up to it, with pools, mud and
    grass on it (QcWetlandPattern, decision E-661).

    Beyond, the ground returns to the terrain at most a block per block of distance (more than
    the terrain's own slope only where the edge's noise bends), so no cliff cuts the neighbouring
    land however high it lies: ground far above the water is only cut a few blocks near the
    village. The land access beside the stairs stays as it is. A pure function of the seed, the
    plan and the terrain noise per column, so every chunk shapes its own columns the same in any
    order. Thread-safe. */
class QcWetlandShaper
{
public:
    /*! \brief A shaped column: the height of its top block (\c {top}) and what covers it
        (\c {cover}). \qcfields */
    struct Column
    {
        int top = 0;
        QcWetlandPattern::Cover cover = QcWetlandPattern::Cover::Grass;
    };

    /*! Shapes the ground over \a terrain, its noise seeded from \a seed. */
    QcWetlandShaper(const QcTerrainGenerator &terrain, quint64 seed);

    /*! Returns the height of the ground's top block the column \a x, \a z gets from the
        wetland of \a plan (a pool's bottom under water); std::nullopt keeps the terrain (no
        wetland, outside it, in the land access, or dry ground the blend leaves as it is). */
    [[nodiscard]] std::optional<int> groundAt(const QcVillagePlan &plan, int x, int z) const;
    /*! Returns the shaped column \a x, \a z of \a plan with its cover; std::nullopt as for
        groundAt(). */
    [[nodiscard]] std::optional<Column> columnAt(const QcVillagePlan &plan, int x, int z) const;
    /*! Shapes the columns of \a chunk in the wetland of \a plan, before the village is built
        into it:
        lowered ground is removed (water up to the water surface, air above), raised ground
        filled with the bottom, the top block is the bottom under water and on mud, else the
        terrain's surface block; cave air from the water surface down to below the deepest pool
        is filled with the bottom; the style's plant grows on some columns beside the pools and
        its floating plant on some pool columns. */
    void shape(QcChunk &chunk, const QcVillagePlan &plan) const;

private:
    // columnAt() for a column whose terrain ground's top block is `ground`.
    [[nodiscard]] std::optional<Column> targetAt(const QcVillagePlan &plan, int x, int z, int ground) const;
    // The height of the column's ground before pools are cut: the water surface within the edge,
    // returning to the terrain's `ground` towards the blend's end; `ground` outside the wetland.
    [[nodiscard]] int levelAt(const QcVillagePlan &plan, int x, int z, int ground) const;
    // Whether a pool may be cut at the column: none of its sides lies above the water surface.
    [[nodiscard]] bool poolFits(const QcVillagePlan &plan, int x, int z) const;
    // Shapes one column: `local` is the column in the chunk with y the terrain ground's top block.
    void shapeColumn(QcChunk &chunk, QcBlockPos local, Column column, const QcVillageStyle::Wetland &settings) const;
    // The style's plant on a column at the water surface beside a pool, where it grows, and its
    // floating plant on the water of a pool column.
    void plant(QcChunk &chunk, QcBlockPos local, const QcVillagePlan &plan) const;
    // Distance from the column to the nearest piece or boardwalk (0 inside one).
    static double distanceToWet(const QcVillagePlan::Wetland &wetland, int x, int z);

    const QcTerrainGenerator &m_terrain;
    QcNoise m_fray;
    QcWetlandPattern m_pattern;
};
