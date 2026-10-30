// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/worldgen/QcMineshaftDefinition.h"
#include "worldgen/mineshafts/QcMineshaftNetwork.h"
#include "worldgen/mineshafts/QcMineshaftPlan.h"

/*! \brief Where the lattice of a mine shaft lies in the world (decision E-570), and the shapes of
    its nodes and corridors there: lattice points \c {spacing} blocks apart around the central room,
    levels \c {levelSpacing} blocks apart downwards.

    A corridor fills the gap between the boxes of its two nodes, with supports spread evenly; a
    staircase starts one cell beside its upper node, one lower, and ends level with its lower
    node. */
struct QcMineshaftFrame
{
    /*! A lattice point: offsets from the central room and the level. */
    using Point = QcMineshaftNetwork::Point;

    /*! Feet cell in the middle of the central room. */
    QcBlockPos centre;
    /*! Blocks between neighbouring lattice points (QcMineshaftSettings::spacing()). */
    int spacing = 9;
    /*! Blocks between levels, downwards. */
    int levelSpacing = 6;
    /*! Cells of the central room on each side of its middle. */
    int roomHalf = 4;
    /*! Height of the central room's open space. */
    int roomHeight = 4;
    /*! Cells between the supports of a level corridor. */
    int supportSpacing = 3;

    QcMineshaftFrame(QcBlockPos middle, const QcMineshaftSettings &settings)
        : centre(middle)
        , spacing(settings.spacing())
        , levelSpacing(settings.levelSpacing)
        , roomHalf(settings.roomHalf)
        , roomHeight(settings.roomHeight)
        , supportSpacing(settings.supportSpacing)
    {
    }

    /*! Returns whether \a at is the central room's point (the origin of the lattice). */
    static bool isRoom(Point at) { return at == Point{}; }
    /*! Returns the feet cell in the middle of the node at \a point. */
    QcBlockPos at(Point point) const { return centre.offset(point.x * spacing, -point.level * levelSpacing, point.z * spacing); }
    /*! Returns the cells of the node at \a point on each side of its middle: \c {roomHalf} for
        the central room, 1 for a junction. */
    int halfOf(Point point) const { return isRoom(point) ? roomHalf : 1; }

    /*! Returns the node at \a point: its lattice position, middle, half size and height
        (\c {roomHeight} for the central room, 3 for a junction). */
    QcMineshaftPlan::Node node(Point point) const
    {
        QcMineshaftPlan::Node node;
        node.x = point.x;
        node.z = point.z;
        node.level = point.level;
        node.centre = at(point);
        node.half = halfOf(point);
        node.height = isRoom(point) ? roomHeight : 3;
        return node;
    }

    /*! Returns the corridor from \a from to its neighbour \a to (a staircase when \a to lies one
        level lower). */
    QcMineshaftPlan::Corridor corridor(Point from, Point to) const
    {
        QcMineshaftPlan::Corridor corridor;
        corridor.dx = to.x - from.x;
        corridor.dz = to.z - from.z;
        const int first = halfOf(from) + 1;
        corridor.start = at(from).offset(corridor.dx * first, 0, corridor.dz * first);
        corridor.length = spacing - halfOf(from) - halfOf(to) - 1;
        if (to.level != from.level) {
            corridor.start = corridor.start.offset(0, -1, 0);
            corridor.rise = -1;
            return corridor;
        }
        // Supports every `supportSpacing` cells, as far from both ends.
        for (int along = (corridor.length - 1) % supportSpacing / 2; along < corridor.length; along += supportSpacing) {
            corridor.supports.append(along);
        }
        return corridor;
    }
};
