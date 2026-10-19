#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "common/core/QcBlockPos.h"

#include <QList>

#include <algorithm>
#include <QString>

#include <utility>

// One dungeon as the terrain generator builds it (decision E-522): a pure function of the seed, the
// region and the content, planned from the terrain noise alone (never from generated chunks), so
// every chunk writes its own cells of it and a dungeon spanning chunks comes out the same in any
// generation order (QcDungeonGenerator::build).
struct QcDungeonPlan
{
    // A block placed at a cell (chests, lights).
    struct Placed
    {
        QcBlockPos at;
        QcBlockId block = qcAirBlockId;
    };

    // A corridor from the room to a cave: the feet cells of its path from the door in the room's
    // wall outwards, the last one in the cave; each is open two cells high.
    struct Corridor
    {
        QList<QcBlockPos> path;
        // Directions of the steps (x, z), one per cell after the first.
        QList<std::pair<int, int>> steps;
    };

    // The region it was planned for.
    int regionX = 0;
    int regionZ = 0;
    // The room: its lower north west corner and outer sizes (walls included).
    QcBlockPos origin;
    int sizeX = 0;
    int sizeY = 5;
    int sizeZ = 0;
    QcBlockPos spawner;
    QString mob;
    QList<Placed> chests;
    QList<Corridor> corridors;
    // Cells next to the corridors (floor, ceiling, sides) that get a wall block where they are not
    // rock; none of them lies in the room or in a corridor.
    QList<QcBlockPos> lining;
    QList<Placed> lights;
    // The spawner's spawn area (inclusive corners).
    QcBlockPos areaLow;
    QcBlockPos areaHigh;
    // Columns touched by the room, its corridors and their lining (inclusive).
    int minX = 0;
    int minZ = 0;
    int maxX = 0;
    int maxZ = 0;

    bool inRoom(QcBlockPos pos) const
    {
        return pos.x >= origin.x && pos.x < origin.x + sizeX && pos.y >= origin.y && pos.y < origin.y + sizeY && pos.z >= origin.z
               && pos.z < origin.z + sizeZ;
    }
    bool touches(int x0, int z0, int x1, int z1) const { return x1 >= minX && x0 <= maxX && z1 >= minZ && z0 <= maxZ; }
    // Sets the columns the plan touches from its room, its corridors and their lining, and one column
    // more around every corridor cell, where liquid beside the path is sealed (the cave end has no
    // lining; a pool across a chunk edge from it is sealed by the neighbouring chunk, which must run
    // the plan too).
    void bound()
    {
        minX = origin.x;
        minZ = origin.z;
        maxX = origin.x + sizeX - 1;
        maxZ = origin.z + sizeZ - 1;
        const auto add = [this](QcBlockPos cell) {
            minX = std::min(minX, cell.x);
            minZ = std::min(minZ, cell.z);
            maxX = std::max(maxX, cell.x);
            maxZ = std::max(maxZ, cell.z);
        };
        for (const QcBlockPos &cell : std::as_const(lining)) {
            add(cell);
        }
        for (const Corridor &corridor : std::as_const(corridors)) {
            for (const QcBlockPos &feet : corridor.path) {
                add(feet.offset(-1, 0, -1));
                add(feet.offset(1, 0, 1));
            }
        }
    }
    // Taxicab distance from `pos` to the nearest cell of the spawn area.
    int distanceToArea(QcBlockPos pos) const
    {
        const auto axis = [](int value, int low, int high) { return value < low ? low - value : (value > high ? value - high : 0); };
        return axis(pos.x, areaLow.x, areaHigh.x) + axis(pos.y, areaLow.y, areaHigh.y) + axis(pos.z, areaLow.z, areaHigh.z);
    }
};
