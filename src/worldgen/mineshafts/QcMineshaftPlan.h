// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "common/core/QcBlockPos.h"
#include "worldgen/structures/QcStructurePlacement.h"

#include <QList>
#include <QString>

#include <algorithm>
#include <cstdlib>
#include <optional>

// One mine shaft as the terrain generator builds it (decision E-570): a pure function of the seed,
// the region and the content, planned from the terrain noise alone (never from generated chunks),
// so every chunk writes its own cells of it and a mine shaft spanning chunks comes out the same in
// any generation order (QcMineshaftBuilder). Cells are world positions; a corridor's or a node's
// "feet" cells are the lowest open cells, its floor lies one below.
struct QcMineshaftPlan
{
    // A block placed at a cell.
    struct Placed
    {
        QcBlockPos at;
        QcBlockId block = qcAirBlockId;
    };

    // A crossing, corner or dead end of a level, or the central room (node 0): an open box of
    // (2 half + 1)^2 cells, `height` high above its floor.
    struct Node
    {
        // Lattice point and level (0 the top).
        int x = 0;
        int z = 0;
        int level = 0;
        QcBlockPos centre;
        int half = 1;
        int height = 3;
        // Its floor crosses a cave somewhere: planks where it is open, a pillar under the centre.
        bool bridge = false;

        bool contains(QcBlockPos pos) const
        {
            return std::abs(pos.x - centre.x) <= half && std::abs(pos.z - centre.z) <= half && pos.y >= centre.y && pos.y < centre.y + height;
        }
    };

    // A straight corridor three cells wide from node `from` to node `to`: `length` cells from
    // `start` (feet, middle lane) along (dx, dz), each `rise` higher than the one before: 0 on a
    // level, -1 for a staircase down to the next level (four cells high, so one can jump up).
    struct Corridor
    {
        int from = 0;
        int to = 0;
        QcBlockPos start;
        int dx = 0;
        int dz = 0;
        int length = 0;
        int rise = 0;
        // Its floor crosses a cave somewhere: planks where it is open, pillars under the supports.
        bool bridge = false;
        bool rails = false;
        // Cells along it with a support (posts on both sides, a beam above).
        QList<int> supports;

        bool stairs() const { return rise != 0; }
        int height() const { return stairs() ? 4 : 3; }
        // The feet cell `along` cells from the start, `lateral` cells to the right of the middle
        // lane (seen along the corridor).
        QcBlockPos cell(int along, int lateral = 0) const { return start.offset(dx * along - dz * lateral, rise * along, dz * along + dx * lateral); }
    };

    // A room from a structure (QcMineshaftSettings rooms) at the dead end `node`.
    struct Room
    {
        int node = 0;
        QcStructurePlacement placement;
    };

    // The way up to the surface (QcMineshaftAccess): a ladder shaft from the central room through
    // the ground under a headframe, or an inclined tunnel from a side of the central room up to the
    // ground (decision E-895). The terrain generator builds it after its trees, cacti and surface
    // pools (QcMineshaftBuilder::buildEntrance), so none of them blocks it.
    struct Entrance
    {
        enum class Kind : quint8 {
            Ladder,
            Incline,
        };

        Kind kind = Kind::Ladder;
        // Ladder: the shaft's top cell, in the ground's top block. Incline: the ground's top block
        // in front of the tunnel's mouth. The box of 3 x 3 x 3 cells above it is cleared, and the
        // leaves and cacti of the 5 x 5 ring around that box.
        QcBlockPos top;
        // The open cell at the top of the way up, from which one steps onto the ground: two above
        // `top` for the ladder, the last feet cell of the tunnel's middle lane for an incline.
        QcBlockPos out;
        // The open cells: the ladder shaft from above the central room to two cells above `top`, or
        // the tunnel.
        QList<QcBlockPos> shaft;
        // Cells around the shaft (under the tunnel's steps) that become planks where they are not solid.
        QList<QcBlockPos> lining;
        // The ladder from the room's floor up and the headframe, or the tunnel's supports and rails.
        QList<Placed> blocks;
    };

    // A spawner with its spawn area (inclusive corners).
    struct Spawner
    {
        QcBlockPos at;
        QcBlockPos areaLow;
        QcBlockPos areaHigh;
    };

    // The region it was planned for.
    int regionX = 0;
    int regionZ = 0;
    int levels = 0;
    QList<Node> nodes;
    QList<Corridor> corridors;
    QList<Room> rooms;
    QList<Spawner> spawners;
    // Rail cells that hold a chest minecart.
    QList<QcBlockPos> carts;
    // Supports, rails, cobwebs, lights and spawners, placed after the corridors are open.
    QList<Placed> blocks;
    // Floor cells with a pillar down to the ground below them where the floor is open.
    QList<QcBlockPos> pillars;
    std::optional<Entrance> entrance;
    // Columns touched by everything above and one more around (inclusive).
    int minX = 0;
    int minZ = 0;
    int maxX = 0;
    int maxZ = 0;

    bool touches(int x0, int z0, int x1, int z1) const { return x1 >= minX && x0 <= maxX && z1 >= minZ && z0 <= maxZ; }
    // Sets the columns touched by its open cells, blocks, entrance and rooms, and one more around.
    void bound()
    {
        const QcBlockPos centre = nodes.constFirst().centre;
        minX = maxX = centre.x;
        minZ = maxZ = centre.z;
        const auto add = [this](QcBlockPos cell) {
            minX = std::min(minX, cell.x - 1);
            minZ = std::min(minZ, cell.z - 1);
            maxX = std::max(maxX, cell.x + 1);
            maxZ = std::max(maxZ, cell.z + 1);
        };
        forEachOpenCell(add);
        for (const QcMineshaftPlan::Placed &placed : std::as_const(blocks)) {
            add(placed.at);
        }
        if (entrance) {
            // The cleared box over `top` and the ring of foliage around it (QcMineshaftBuilder::buildEntrance).
            add(entrance->top.offset(-2, 0, -2));
            add(entrance->top.offset(2, 0, 2));
            for (const QcMineshaftPlan::Placed &placed : std::as_const(entrance->blocks)) {
                add(placed.at);
            }
            for (const QcBlockPos &cell : std::as_const(entrance->lining)) {
                add(cell);
            }
        }
        for (const QcMineshaftPlan::Room &room : std::as_const(rooms)) {
            add(room.placement.origin());
            add(room.placement.origin().offset(room.placement.sizeX() - 1, 0, room.placement.sizeZ() - 1));
        }
    }
    // Calls `visit` with every cell the plan opens: nodes, corridors and the entrance's shaft.
    template<typename Visit>
    void forEachOpenCell(const Visit &visit) const
    {
        for (const Node &node : nodes) {
            forEachCell(node, visit);
        }
        for (const Corridor &corridor : corridors) {
            forEachCell(corridor, visit);
        }
        if (!entrance) {
            return;
        }
        for (const QcBlockPos &cell : entrance->shaft) {
            visit(cell);
        }
    }
    template<typename Visit>
    static void forEachCell(const Node &node, Visit &visit)
    {
        const int side = 2 * node.half + 1;
        for (int dy = 0; dy < node.height; ++dy) {
            for (int i = 0; i < side * side; ++i) {
                visit(node.centre.offset(i % side - node.half, dy, i / side - node.half));
            }
        }
    }
    template<typename Visit>
    static void forEachCell(const Corridor &corridor, Visit &visit)
    {
        for (int along = 0; along < corridor.length; ++along) {
            for (int i = 0; i < 3 * corridor.height(); ++i) {
                visit(corridor.cell(along, i % 3 - 1).offset(0, i / 3, 0));
            }
        }
    }
};
