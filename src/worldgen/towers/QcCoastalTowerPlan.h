// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcXzRect.h"
#include "worldgen/structures/QcStructurePlacement.h"

/*! \brief One coastal tower as the terrain generator builds it (decision E-605): a pure function of
    the seed, the region and the content, planned from the terrain noise alone (never from generated
    chunks), so every chunk writes its own cells of it and a tower spanning chunks comes out the
    same in any generation order (QcCoastalTowerGenerator::build).

    The structure's lowest layer replaces the ground's top block of the highest column under it;
    its door is the middle of the structure's south side, one layer above the floor. */
struct QcCoastalTowerPlan
{
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
    static QcXzRect footprintOf(const QcStructurePlacement &placement)
    {
        const QcBlockPos origin = placement.origin();
        return {origin.x, origin.z, origin.x + placement.sizeX() - 1, origin.z + placement.sizeZ() - 1};
    }
    /*! Returns the columns covered by the tower. */
    QcXzRect footprint() const { return footprintOf(placement); }
};
