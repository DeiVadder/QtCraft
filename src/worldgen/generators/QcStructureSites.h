// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcWorldConstants.h"
#include "worldgen/generators/QcGuardSite.h"
#include "worldgen/structures/QcStructurePlacement.h"

#include <QList>
#include <QStringList>

#include <algorithm>

/*! \brief The footprint of a placed structure over the whole height of the world in which the
    natural monster spawns bring forth only its kinds (decision E-871, the spawn overrides of the
    original game's structures: the witch hut and its witches).

    Like guardian zones it comes from the plans alone and needs no saving. */
struct QcSpawnArea
{
    /*! The lowest and the highest corner of the footprint in world cells, both inside; y spans
        the world. \qcshared */
    QcBlockPos min;
    QcBlockPos max;
    /*! The structure's \c {monsters} (names). */
    QStringList monsters;

    /*! Returns whether the column \a x, \a z lies in the footprint. */
    bool containsColumn(int x, int z) const { return x >= min.x && z >= min.z && x <= max.x && z <= max.z; }

    /*! Returns the spawn area of \a placement, a placed copy of a structure, if it has one. */
    static std::optional<QcSpawnArea> of(const QcStructurePlacement &placement)
    {
        const std::optional<QcStructureDefinition::SpawnArea> &rule = placement.structure().spawnArea;
        if (!rule) {
            return std::nullopt;
        }
        const QcBlockPos a = placement.toWorld(QcBlockPos{0, 0, 0});
        const QcBlockPos b = placement.toWorld(QcBlockPos{placement.structure().sizeX() - 1, 0, placement.structure().sizeZ() - 1});
        return QcSpawnArea{{std::min(a.x, b.x), 0, std::min(a.z, b.z)}, {std::max(a.x, b.x), QcWorld::worldHeight - 1, std::max(a.z, b.z)},
                           rule->monsters};
    }
};

/*! \brief What the placed structures near a place bring forth by themselves
    (QcWorldGenerator::structureSitesNear): their guardian zones (decision E-591) and their spawn
    areas (decision E-871). */
struct QcStructureSites
{
    /*! The guardian zones. */
    QList<QcGuardSite> guards;
    /*! The spawn areas. */
    QList<QcSpawnArea> spawnAreas;

    /*! Returns the sites of \a placement, a placed copy of a structure. */
    static QcStructureSites of(const QcStructurePlacement &placement)
    {
        QcStructureSites sites{QcGuardSite::of(placement), {}};
        if (const std::optional<QcSpawnArea> area = QcSpawnArea::of(placement)) {
            sites.spawnAreas.append(*area);
        }
        return sites;
    }

    /*! Adds the sites of \a other. */
    void add(const QcStructureSites &other)
    {
        guards.append(other.guards);
        spawnAreas.append(other.spawnAreas);
    }
};
