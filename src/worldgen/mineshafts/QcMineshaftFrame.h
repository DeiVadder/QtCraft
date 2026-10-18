#pragma once

#include "common/content/definitions/QcMineshaftDefinition.h"
#include "worldgen/mineshafts/QcMineshaftNetwork.h"
#include "worldgen/mineshafts/QcMineshaftPlan.h"

// Where the lattice of a mine shaft lies in the world (decision E-570), and the shapes of its nodes
// and corridors there: lattice points `spacing` blocks apart around the central room, levels
// `levelSpacing` blocks apart downwards. A corridor fills the gap between the boxes of its two
// nodes, with supports spread evenly; a staircase starts one cell beside its upper node, one lower,
// and ends level with its lower node.
struct QcMineshaftFrame
{
    using Point = QcMineshaftNetwork::Point;

    // Feet cell in the middle of the central room.
    QcBlockPos centre;
    int spacing = 9;
    int levelSpacing = 6;
    int roomHalf = 4;
    int roomHeight = 4;
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

    static bool isRoom(Point at) { return at == Point{}; }
    QcBlockPos at(Point point) const { return centre.offset(point.x * spacing, -point.level * levelSpacing, point.z * spacing); }
    int halfOf(Point point) const { return isRoom(point) ? roomHalf : 1; }

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

    // The corridor from `from` to its neighbour `to` (a staircase when `to` lies one level lower).
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
