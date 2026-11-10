// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/fortress/QcFortressRooms.h"

#include "worldgen/terrain/QcNoise.h"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace {

using Plan = QcFortressPlan;
using Room = QcFortressPlan::Room;
using Side = QcFortressPlan::Side;

constexpr std::array<Side, 4> sides{Side::North, Side::East, Side::South, Side::West};
// The width of the lanes between the beds of a garden, from their middle.
constexpr int aisle = 1;
// The cells a stairwell's flight runs along its wall: the first and the last step from the middle.
constexpr int flightFirst = -3;
constexpr int flightLast = 3;
// How far from the middle the flight lies, its inner row, and the rail along the hole.
constexpr int flightRow = 4;
constexpr int railRow = 3;


// The red lanes from the middle to the doors.
void layLanes(QcFortressCanvas &canvas, const Room &room)
{
    canvas.set(room.centre, canvas.blocks().lane);
    for (const Side side : sides) {
        if (room.doors[static_cast<std::size_t>(side)]) {
            for (int out = 1; out <= room.half; ++out) {
                canvas.set(Plan::onSide(room.centre, side, out, 0), canvas.blocks().lane);
            }
        }
    }
}

// The low wall and rail around an open room, with gaps at the doors.
void raiseParapet(QcFortressCanvas &canvas, const Room &room)
{
    const QcBlockPos feet = room.centre.offset(0, 1, 0);
    for (const Side side : sides) {
        const bool door = room.doors[static_cast<std::size_t>(side)];
        for (int along = -room.half; along <= room.half; ++along) {
            if (door && std::abs(along) < Plan::passageHalf) {
                continue;
            }
            const QcBlockPos cell = Plan::onSide(feet, side, room.half, along);
            const bool corner = std::abs(along) == room.half;
            canvas.set(cell, corner ? canvas.blocks().chiseled : canvas.blocks().brick);
            canvas.set(cell.offset(0, 1, 0), corner ? canvas.blocks().fencePost : canvas.blocks().fenceAlong(Plan::clockwise(side)));
        }
    }
}

// Ribs in the corners of a closed room, and the light in its roof.
void dressHall(QcFortressCanvas &canvas, const Room &room)
{
    const int inner = room.half - 1;
    const int roof = room.topFloor() + Plan::roomHeight + 1;
    for (const int x : {-inner, inner}) {
        for (const int z : {-inner, inner}) {
            const QcBlockPos corner = room.centre.offset(x, 0, z);
            canvas.fill({{corner.x, room.topFloor() + 1, corner.z}, {corner.x, roof - 1, corner.z}}, canvas.blocks().chiseled);
        }
    }
    canvas.set({room.centre.x, roof, room.centre.z}, canvas.blocks().light);
}

void furnishCrossing(QcFortressCanvas &canvas, const Room &room)
{
    layLanes(canvas, room);
    if (room.roofed) {
        dressHall(canvas, room);
    } else {
        raiseParapet(canvas, room);
    }
}

void furnishPlatform(QcFortressCanvas &canvas, const Room &room)
{
    layLanes(canvas, room);
    raiseParapet(canvas, room);
    // A dais of nine cells carries the spawner; a post with a light stands at each inner corner.
    const QcBlockPos feet = room.centre.offset(0, 1, 0);
    canvas.fill({feet.offset(-1, 0, -1), feet.offset(1, 0, 1)}, canvas.blocks().chiseled);
    canvas.set(feet.offset(0, 1, 0), canvas.blocks().spawner);
    const int reach = room.half - 2;
    for (const int x : {-reach, reach}) {
        for (const int z : {-reach, reach}) {
            canvas.fill({feet.offset(x, 0, z), feet.offset(x, 1, z)}, canvas.blocks().chiseled);
            canvas.set(feet.offset(x, 2, z), canvas.blocks().light);
        }
    }
}

void furnishGarden(QcFortressCanvas &canvas, const Room &room)
{
    layLanes(canvas, room);
    dressHall(canvas, room);
    // Four raised beds in the corners of the hall, around the ribs; what ripens differs from cell
    // to cell.
    const QcBlockPos feet = room.centre.offset(0, 1, 0);
    const int inner = room.half - 1;
    for (int z = -inner; z <= inner; ++z) {
        for (int x = -inner; x <= inner; ++x) {
            const bool rib = std::abs(x) == inner && std::abs(z) == inner;
            if (std::abs(x) <= aisle || std::abs(z) <= aisle || rib) {
                continue;
            }
            const QcBlockPos bed = feet.offset(x, 0, z);
            canvas.set(bed, canvas.blocks().soil);
            const auto age = static_cast<std::size_t>(qcPositionRandom(0x6a7d, bed.x, bed.y, bed.z) * 4.0);
            canvas.set(bed.offset(0, 1, 0), canvas.blocks().crops[std::min<std::size_t>(age, 3)]);
        }
    }
}

// The upper floor of a stairwell with the hole over its flight, and the rail along the hole.
void layUpperFloor(QcFortressCanvas &canvas, const Room &room)
{
    const int inner = room.half - 1;
    const QcBlockPos upper = {room.centre.x, room.topFloor(), room.centre.z};
    for (int out = -inner; out <= inner; ++out) {
        for (int along = -inner; along <= inner; ++along) {
            const bool hole = out >= flightRow && along <= flightLast;
            if (hole) {
                continue;
            }
            canvas.brick({Plan::onSide(upper, room.flight, out, along), Plan::onSide(upper, room.flight, out, along)});
            if (out == railRow && along <= flightLast) {
                canvas.set(Plan::onSide(upper, room.flight, out, along).offset(0, 1, 0), canvas.blocks().fenceAlong(Plan::clockwise(room.flight)));
            }
        }
    }
    for (const Side side : sides) {
        if (room.upperDoors[static_cast<std::size_t>(side)]) {
            for (int out = 1; out <= room.half; ++out) {
                canvas.set(Plan::onSide(upper, side, out, 0), canvas.blocks().lane);
            }
        }
    }
}

// The flight from the lower floor to the upper one, two cells wide along the wall.
void buildFlight(QcFortressCanvas &canvas, const Room &room)
{
    const QcBlockId stairs = canvas.blocks().stairsTo(Plan::clockwise(room.flight));
    const bool door = room.doors[static_cast<std::size_t>(room.flight)];
    for (int along = flightFirst; along <= flightLast; ++along) {
        const int height = along - flightFirst + 1;
        for (const int out : {flightRow, flightRow + 1}) {
            const QcBlockPos step = Plan::onSide(room.centre, room.flight, out, along).offset(0, height, 0);
            canvas.set(step, stairs);
            // Solid below, but free under the steps a door on this wall leads beneath.
            if (!door || std::abs(along) > 1) {
                canvas.brick({step.offset(0, 1 - height, 0), step.offset(0, -1, 0)});
            }
        }
    }
}

void furnishStairwell(QcFortressCanvas &canvas, const Room &room)
{
    layLanes(canvas, room);
    dressHall(canvas, room);
    layUpperFloor(canvas, room);
    buildFlight(canvas, room);
    // The lower floor has its own light in the underside of the upper one.
    canvas.set(room.centre.offset(0, Plan::levelRise, 0), canvas.blocks().light);
}

} // namespace

namespace QcFortressRooms {

void furnish(QcFortressCanvas &canvas, const Room &room)
{
    switch (room.kind) {
    case Plan::RoomKind::Crossing:
        furnishCrossing(canvas, room);
        break;
    case Plan::RoomKind::Stairwell:
        furnishStairwell(canvas, room);
        break;
    case Plan::RoomKind::Platform:
        furnishPlatform(canvas, room);
        break;
    case Plan::RoomKind::Garden:
        furnishGarden(canvas, room);
        break;
    }
}

} // namespace QcFortressRooms
