// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/strongholds/QcStrongholdPlan.h"

using Plan = QcStrongholdPlan;

Plan::Shape Plan::shapeOf(Kind kind, bool tall)
{
    // The sizes of the pieces of the original game's Java edition.
    using enum Exit;
    switch (kind) {
    case Kind::Start:
    case Kind::SpiralStairs:
        return {5, 11, 5, 2, 7, {{{Forward, 2, 1, 4}}}, 1};
    case Kind::Corridor:
        return {5, 5, 7, 2, 1, {{{Forward, 2, 1, 6}, {Left, 0, 1, 3}, {Right, 4, 1, 3}}}, 3};
    case Kind::ChestCorridor:
        return {5, 5, 7, 2, 1, {{{Forward, 2, 1, 6}}}, 1};
    case Kind::Prison:
        return {9, 5, 11, 2, 1, {{{Forward, 2, 1, 10}}}, 1};
    case Kind::LeftTurn:
        return {5, 5, 5, 2, 1, {{{Left, 0, 1, 2}}}, 1};
    case Kind::RightTurn:
        return {5, 5, 5, 2, 1, {{{Right, 4, 1, 2}}}, 1};
    case Kind::Crossing:
        return {11, 7, 11, 5, 1, {{{Forward, 5, 1, 10}, {Left, 0, 1, 5}, {Right, 10, 1, 5}}}, 3};
    case Kind::StraightStairs:
        return {5, 11, 8, 2, 7, {{{Forward, 2, 1, 7}}}, 1};
    case Kind::Library:
        return {14, tall ? 11 : 6, 15, 6, 1, {}, 0};
    case Kind::PortalRoom:
        break;
    }
    return {11, 8, 16, 5, 1, {}, 0};
}

QcBlockPos Plan::Piece::at(int x, int y, int z) const
{
    const int worldY = box.low.y + y;
    switch (facing) {
    case Side::North:
        return {box.low.x + x, worldY, box.high.z - z};
    case Side::South:
        return {box.high.x - x, worldY, box.low.z + z};
    case Side::East:
        return {box.low.x + z, worldY, box.low.z + x};
    case Side::West:
        break;
    }
    return {box.high.x - z, worldY, box.high.z - x};
}

QcBlockBox Plan::Piece::span(QcBlockPos a, QcBlockPos b) const
{
    const QcBlockPos one = at(a.x, a.y, a.z);
    const QcBlockPos other = at(b.x, b.y, b.z);
    return {{std::min(one.x, other.x), std::min(one.y, other.y), std::min(one.z, other.z)},
            {std::max(one.x, other.x), std::max(one.y, other.y), std::max(one.z, other.z)}};
}

Plan::Side Plan::Piece::sideOf(Exit exit) const
{
    switch (exit) {
    case Exit::Left:
        return left();
    case Exit::Right:
        return right();
    case Exit::Forward:
        break;
    }
    return facing;
}

QcBlockBox Plan::Piece::placed(Kind kind, bool tall, Side facing, QcBlockPos door)
{
    // Laid out at the origin first, then moved so its doorway lands on `door`.
    const Shape shape = shapeOf(kind, tall);
    const bool across = facing == Side::East || facing == Side::West;
    const int sizeX = across ? shape.depth : shape.width;
    const int sizeZ = across ? shape.width : shape.depth;
    const Piece piece{kind, facing, {{0, 0, 0}, {sizeX - 1, shape.height - 1, sizeZ - 1}}};
    const QcBlockPos doorway = piece.at(shape.doorX, shape.doorY, 0);
    const QcBlockPos shift{door.x - doorway.x, door.y - doorway.y, door.z - doorway.z};
    return {piece.box.low.offset(shift.x, shift.y, shift.z), piece.box.high.offset(shift.x, shift.y, shift.z)};
}
