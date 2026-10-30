// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/structures/QcStructurePlacement.h"

/*! \brief One coastal tower as the terrain generator builds it (decision E-605): a pure function of
    the seed, the region and the content, planned from the terrain noise alone (never from generated
    chunks), so every chunk writes its own cells of it and a tower spanning chunks comes out the
    same in any generation order (QcCoastalTowerGenerator::build).

    The structure's lowest layer replaces the ground's top block of the highest column under it;
    its door is the middle of the structure's south side, one layer above the floor. */
struct QcCoastalTowerPlan
{
    /*! \brief Columns covered by a tower (inclusive). */
    struct Footprint
    {
        /*! The lowest world x covered. */
        int minX = 0;
        /*! The lowest world z covered. */
        int minZ = 0;
        /*! The highest world x covered. */
        int maxX = 0;
        /*! The highest world z covered. */
        int maxZ = 0;

        /*! Returns whether the column \a x, \a z lies in the footprint. */
        bool contains(int x, int z) const { return x >= minX && x <= maxX && z >= minZ && z <= maxZ; }
        /*! Returns whether \a other shares at least one column with this footprint. */
        bool touches(Footprint other) const
        {
            return other.maxX >= minX && other.minX <= maxX && other.maxZ >= minZ && other.minZ <= maxZ;
        }
        /*! Returns the footprint grown by \a margin columns on every side. */
        Footprint grown(int margin) const { return {minX - margin, minZ - margin, maxX + margin, maxZ + margin}; }
        /*! Returns the chunks the columns lie in. */
        QList<QcChunkPos> chunks() const
        {
            QList<QcChunkPos> found;
            const QcChunkPos low = QcBlockPos{minX, 0, minZ}.chunk();
            const QcChunkPos high = QcBlockPos{maxX, 0, maxZ}.chunk();
            for (int cz = low.z; cz <= high.z; ++cz) {
                for (int cx = low.x; cx <= high.x; ++cx) {
                    found.append({cx, cz});
                }
            }
            return found;
        }
    };

    /*! The x of the region it was planned for. */
    int regionX = 0;
    /*! The z of the region it was planned for. */
    int regionZ = 0;
    /*! The tower's structure, turned and placed in the world. */
    QcStructurePlacement placement;
    /*! The nearest column of the open sea found around the footprint. */
    QcBlockPos ocean;
    /*! The ground's top block in front of the tower's door. */
    QcBlockPos doorstep;

    /*! Returns the columns covered by \a placement. */
    static Footprint footprintOf(const QcStructurePlacement &placement)
    {
        const QcBlockPos origin = placement.origin();
        return {origin.x, origin.z, origin.x + placement.sizeX() - 1, origin.z + placement.sizeZ() - 1};
    }
    /*! Returns the columns covered by the tower. */
    Footprint footprint() const { return footprintOf(placement); }
};
