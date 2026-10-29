// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worldgen/structures/QcStructurePlacement.h"

// One coastal tower as the terrain generator builds it (decision E-605): a pure function of the
// seed, the region and the content, planned from the terrain noise alone (never from generated
// chunks), so every chunk writes its own cells of it and a tower spanning chunks comes out the same
// in any generation order (QcCoastalTowerGenerator::build). The structure's lowest layer replaces
// the ground's top block of the highest column under it; its door is the middle of the structure's
// south side, one layer above the floor.
struct QcCoastalTowerPlan
{
    // Columns covered by a tower (inclusive).
    struct Footprint
    {
        int minX = 0;
        int minZ = 0;
        int maxX = 0;
        int maxZ = 0;

        bool contains(int x, int z) const { return x >= minX && x <= maxX && z >= minZ && z <= maxZ; }
        bool touches(Footprint other) const
        {
            return other.maxX >= minX && other.minX <= maxX && other.maxZ >= minZ && other.minZ <= maxZ;
        }
        Footprint grown(int margin) const { return {minX - margin, minZ - margin, maxX + margin, maxZ + margin}; }
        // The chunks the columns lie in.
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

    // The region it was planned for.
    int regionX = 0;
    int regionZ = 0;
    QcStructurePlacement placement;
    // A column of the sea near the tower, and the ground's top block in front of its door.
    QcBlockPos ocean;
    QcBlockPos doorstep;

    static Footprint footprintOf(const QcStructurePlacement &placement)
    {
        const QcBlockPos origin = placement.origin();
        return {origin.x, origin.z, origin.x + placement.sizeX() - 1, origin.z + placement.sizeZ() - 1};
    }
    Footprint footprint() const { return footprintOf(placement); }
};
