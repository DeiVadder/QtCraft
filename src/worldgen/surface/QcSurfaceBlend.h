// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcChunk.h"
#include "worldgen/structures/QcStructurePlacement.h"

#include <optional>

class QcTerrainGenerator;

/*! \brief The ground around a structure on the surface blended to its floor (decision E-889, the
    rule's \c {blend}): within \c {blend} blocks of the footprint the ground may move from the
    terrain towards the floor by \c {slope} blocks per block inward from the blend's outer end, like
    the wetland of a village (decision E-660), so the ground right beside the walls lies level with
    the floor wherever the terrain rises less than the freedom there, and returns to the terrain
    without a cliff.

    Lowered ground loses its plants, raised ground is filled with the ground below; the top block
    is the terrain's surface block. Columns under water keep their ground, and no ground is
    lowered below the sea. A pure function of the placement and the terrain noise per column, the
    same in any order of the chunks. Thread-safe. */
class QcSurfaceBlend
{
public:
    /*! How far the ground may differ from the terrain per block of distance inward from the
        blend's outer end (the wetland's slope). */
    static constexpr double slope = 0.6;

    /*! Blends with the columns and blocks of \a terrain. */
    explicit QcSurfaceBlend(const QcTerrainGenerator &terrain);

    /*! Returns the top block a column at \a distance blocks from the footprint gets, whose
        terrain ground's top block is \a ground, around a floor whose top block is \a floor, for a
        blend of \a blend blocks: the ground moves towards the floor by at most slope times
        (\a blend + 1 − \a distance) blocks. */
    [[nodiscard]] static int levelAt(int floor, int ground, double distance, int blend);
    /*! Returns the horizontal distance in blocks from the column \a x, \a z to the nearest
        column of the footprint of \a placement (0 inside). */
    [[nodiscard]] static double distanceTo(const QcStructurePlacement &placement, int x, int z);
    /*! Returns the new top block of the column \a x, \a z whose terrain ground's top block is
        \a ground, if the blend around \a placement moves it (within \a blend of the footprint,
        outside it, not under water; never below the sea level). */
    [[nodiscard]] std::optional<int> topAt(const QcStructurePlacement &placement, int blend, int x, int z, int ground) const;
    /*! Shapes the columns of \a chunk within \a blend of the footprint of \a placement (not the
        footprint). */
    void shape(QcChunk &chunk, const QcStructurePlacement &placement, int blend) const;

private:
    // Moves one column's ground top from `ground` to `target`: `local` is the column in the chunk.
    void shapeColumn(QcChunk &chunk, QcBlockPos local, int ground, int target) const;

    const QcTerrainGenerator &m_terrain;
};
