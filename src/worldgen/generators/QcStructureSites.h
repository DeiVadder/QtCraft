// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcBlockBox.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcWorldConstants.h"
#include "common/core/QcXzRect.h"
#include "worldgen/generators/QcGuardSite.h"
#include "worldgen/structures/QcStructurePlacement.h"

#include <QList>
#include <QStringList>

#include <algorithm>

/*! \brief Where the natural monster spawns bring forth only a structure's kinds (decision E-871,
    the spawn overrides of the original game's structures): the footprint of a witch hut over the
    whole height of the world, or the boxes of a fortress's parts (decision E-1074).

    Like guardian zones it comes from the plans alone and needs no saving. */
struct QcSpawnArea
{
    /*! The lowest and the highest corner of the area in world cells, both inside: the footprint
        over the whole height, or the box around all parts. \qcshared */
    QcBlockPos min;
    QcBlockPos max;
    /*! The structure's \c {monsters}. */
    QList<QcStructureDefinition::SpawnArea::Monster> monsters;
    /*! The boxes of the parts, each inside min and max; none: the whole box is the area. */
    QList<QcBlockBox> parts;

    /*! Returns whether the column \a x, \a z lies in the footprint (the box around all parts). */
    bool containsColumn(int x, int z) const { return x >= min.x && z >= min.z && x <= max.x && z <= max.z; }
    /*! Returns whether a part of the area holds a cell of the column \a x, \a z. */
    bool touchesColumn(int x, int z) const
    {
        const auto inColumn = [x, z](const QcBlockBox &box) { return x >= box.low.x && z >= box.low.z && x <= box.high.x && z <= box.high.z; };
        return containsColumn(x, z) && (parts.isEmpty() || std::ranges::any_of(parts, inColumn));
    }
    /*! Returns whether the area holds the cell \a feet, where a creature's feet would be. */
    bool contains(QcBlockPos feet) const
    {
        const QcBlockBox whole{min, max};
        return whole.contains(feet) && (parts.isEmpty() || std::ranges::any_of(parts, [feet](const QcBlockBox &box) { return box.contains(feet); }));
    }

    /*! Returns the spawn area of \a parts, which lie in the columns of \a bounds, with \a monsters
        (a structure of many parts, decision E-1074): the box around them all is only its
        outline. */
    static QcSpawnArea ofParts(QcXzRect bounds, const QList<QcStructureDefinition::SpawnArea::Monster> &monsters, const QList<QcBlockBox> &parts)
    {
        QcSpawnArea area{{bounds.minX, QcWorld::worldHeight - 1, bounds.minZ}, {bounds.maxX, 0, bounds.maxZ}, monsters, parts};
        for (const QcBlockBox &part : parts) {
            area.min.y = std::min(area.min.y, part.low.y);
            area.max.y = std::max(area.max.y, part.high.y);
        }
        return area;
    }

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
                           rule->monsters, {}};
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
