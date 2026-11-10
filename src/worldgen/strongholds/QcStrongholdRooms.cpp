// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/strongholds/QcStrongholdRooms.h"

#include "worldgen/terrain/QcNoise.h"

namespace {

using Plan = QcStrongholdPlan;
using Piece = Plan::Piece;
using Kind = Plan::Kind;
using Side = Plan::Side;
using Blocks = QcStrongholdBlocks;

// The crossing's furnishing by variant.
enum class CrossingKind : quint8
{
    Plain = 0,
    Pillar = 1,
    Fountain = 2,
    Store = 3,
};

// The share of the air of a library with cobwebs.
constexpr double cobwebShare = 0.07;
// The floor of the portal room's platform and of the frames on it.
constexpr int platformTop = 3;
constexpr int frameY = 4;

Side back(const Piece &piece)
{
    return QcFortressPlan::opposite(piece.facing);
}

bool isStore(const Piece &piece)
{
    return piece.kind == Kind::Crossing && piece.variant == static_cast<int>(CrossingKind::Store);
}

void furnishPrison(QcStrongholdCanvas &canvas, const Piece &piece)
{
    // A lane along the left wall, bars, and two cells behind them with iron doors.
    const Blocks &blocks = canvas.blocks();
    canvas.fill(piece, {4, 1, 1}, {4, 3, 9}, blocks.barsAcross(piece.right()));
    canvas.wall(piece, {5, 1, 5}, {7, 3, 5});
    for (const int z : {3, 7}) {
        canvas.put(piece, {4, 1, z}, Blocks::of(blocks.ironDoorLower, piece.right()));
        canvas.put(piece, {4, 2, z}, Blocks::of(blocks.ironDoorUpper, piece.right()));
    }
}

void furnishPillar(QcStrongholdCanvas &canvas, const Piece &piece)
{
    const Blocks &blocks = canvas.blocks();
    canvas.wall(piece, {5, 1, 5}, {5, 5, 5});
    canvas.put(piece, {5, 3, 4}, Blocks::of(blocks.torches, piece.facing));
    canvas.put(piece, {5, 3, 6}, Blocks::of(blocks.torches, back(piece)));
    canvas.put(piece, {4, 3, 5}, Blocks::of(blocks.torches, piece.right()));
    canvas.put(piece, {6, 3, 5}, Blocks::of(blocks.torches, piece.left()));
}

void furnishFountain(QcStrongholdCanvas &canvas, const Piece &piece)
{
    // A basin of bricks with still water round a chiseled pillar up to the ceiling.
    const Blocks &blocks = canvas.blocks();
    canvas.wall(piece, {3, 1, 3}, {7, 1, 7});
    canvas.fill(piece, {4, 1, 4}, {6, 1, 6}, blocks.water);
    canvas.fill(piece, {5, 1, 5}, {5, 5, 5}, blocks.chiseled);
}

void furnishStore(QcStrongholdCanvas &canvas, const Piece &piece)
{
    // A wooden gallery in the corner left of the doorway, with a railing, a ladder and the chest.
    const Blocks &blocks = canvas.blocks();
    canvas.fill(piece, {1, 3, 1}, {3, 3, 3}, blocks.planks);
    canvas.fill(piece, {3, 4, 1}, {3, 4, 3}, blocks.fenceAcross(piece.right()));
    canvas.fill(piece, {1, 4, 3}, {2, 4, 3}, blocks.fenceAcross(piece.facing));
    canvas.fill(piece, {2, 1, 1}, {2, 4, 1}, Blocks::of(blocks.ladders, back(piece)));
}

void furnishCrossing(QcStrongholdCanvas &canvas, const Piece &piece)
{
    switch (static_cast<CrossingKind>(piece.variant)) {
    case CrossingKind::Pillar:
        furnishPillar(canvas, piece);
        break;
    case CrossingKind::Fountain:
        furnishFountain(canvas, piece);
        break;
    case CrossingKind::Store:
        furnishStore(canvas, piece);
        break;
    case CrossingKind::Plain:
        break;
    }
}

// The cobwebs of a library in one row across at `y`, `z`, beside the aisle.
void libraryCobwebRow(QcStrongholdCanvas &canvas, const Piece &piece, int y, int z)
{
    for (int x = 1; x <= 12; ++x) {
        if ((x < 5 || x > 7) && canvas.drawAt(piece.at(x, y, z)) < cobwebShare) {
            canvas.put(piece, {x, y, z}, canvas.blocks().cobweb);
        }
    }
}

// The cobwebs of a library, in the air over its floor beside the aisle up to `top`.
void libraryCobwebs(QcStrongholdCanvas &canvas, const Piece &piece, int top)
{
    for (int y = 2; y <= top; ++y) {
        for (int z = 1; z <= 13; ++z) {
            libraryCobwebRow(canvas, piece, y, z);
        }
    }
}

// The shelves along the walls of a library from `low` to `high`, with plank pillars and torches
// every four cells.
void libraryWalls(QcStrongholdCanvas &canvas, const Piece &piece, int low, int high)
{
    const Blocks &blocks = canvas.blocks();
    for (int z = 1; z <= 13; ++z) {
        const QcBlockId block = z % 4 == 0 ? blocks.planks : blocks.bookshelf;
        canvas.fill(piece, {1, low, z}, {1, high, z}, block);
        canvas.fill(piece, {12, low, z}, {12, high, z}, block);
        if (z % 4 == 0) {
            canvas.put(piece, {2, low + 1, z}, Blocks::of(blocks.torches, piece.left()));
            canvas.put(piece, {11, low + 1, z}, Blocks::of(blocks.torches, piece.right()));
        }
    }
    canvas.fill(piece, {2, low, 13}, {11, high, 13}, blocks.bookshelf);
}

void furnishGallery(QcStrongholdCanvas &canvas, const Piece &piece)
{
    // The upper storey: a gallery round three walls at the height of 5, a railing, a ladder.
    const Blocks &blocks = canvas.blocks();
    canvas.fill(piece, {1, 4, 13}, {11, 4, 13}, blocks.bookshelf);
    canvas.fill(piece, {1, 5, 1}, {3, 5, 13}, blocks.planks);
    canvas.fill(piece, {10, 5, 1}, {12, 5, 13}, blocks.planks);
    canvas.fill(piece, {4, 5, 11}, {9, 5, 13}, blocks.planks);
    canvas.fill(piece, {3, 6, 1}, {3, 6, 10}, blocks.fenceAcross(piece.right()));
    canvas.fill(piece, {10, 6, 1}, {10, 6, 10}, blocks.fenceAcross(piece.right()));
    canvas.fill(piece, {3, 6, 11}, {10, 6, 11}, blocks.fenceAcross(piece.facing));
    libraryWalls(canvas, piece, 6, 8);
    canvas.fill(piece, {6, 1, 12}, {6, 5, 12}, Blocks::of(blocks.ladders, piece.facing));
}

void furnishLibrary(QcStrongholdCanvas &canvas, const Piece &piece)
{
    const Blocks &blocks = canvas.blocks();
    const bool tall = piece.variant == 1;
    libraryCobwebs(canvas, piece, tall ? 9 : 4);
    libraryWalls(canvas, piece, 1, 3);
    canvas.fill(piece, {2, 1, 1}, {4, 3, 1}, blocks.bookshelf);
    canvas.fill(piece, {8, 1, 1}, {11, 3, 1}, blocks.bookshelf);
    for (const int z : {5, 9}) {
        canvas.fill(piece, {3, 1, z}, {4, 2, z}, blocks.bookshelf);
        canvas.fill(piece, {8, 1, z}, {10, 2, z}, blocks.bookshelf);
    }
    if (tall) {
        furnishGallery(canvas, piece);
    }
}

void furnishPortalRoom(QcStrongholdCanvas &canvas, const Piece &piece)
{
    // A raised platform with three steps up, the pool of lava under the frames and two channels
    // of lava along its sides, torches on the walls.
    const Blocks &blocks = canvas.blocks();
    canvas.wall(piece, {1, 1, 7}, {9, platformTop, 14});
    for (int step = 0; step < 3; ++step) {
        if (step > 0) {
            canvas.wall(piece, {4, 1, 4 + step}, {6, step, 4 + step});
        }
        canvas.fill(piece, {4, step + 1, 4 + step}, {6, step + 1, 4 + step}, Blocks::of(blocks.stairs, piece.facing));
    }
    canvas.fill(piece, {4, platformTop, 9}, {6, platformTop, 11}, blocks.lava);
    canvas.fill(piece, {1, platformTop, 8}, {1, platformTop, 12}, blocks.lava);
    canvas.fill(piece, {9, platformTop, 8}, {9, platformTop, 12}, blocks.lava);
    for (const int z : {3, 13}) {
        canvas.put(piece, {1, 5, z}, Blocks::of(blocks.torches, piece.left()));
        canvas.put(piece, {9, 5, z}, Blocks::of(blocks.torches, piece.right()));
    }
}

} // namespace

QList<QcStrongholdPlan::Chest> QcStrongholdRooms::chestsOf(const Piece &piece)
{
    if (piece.kind == Kind::ChestCorridor) {
        return {{piece.at(3, 1, 3), piece.left(), Plan::Loot::Corridor}};
    }
    if (isStore(piece)) {
        return {{piece.at(1, 4, 1), piece.right(), Plan::Loot::Store}};
    }
    if (piece.kind != Kind::Library) {
        return {};
    }
    QList<Plan::Chest> chests{{piece.at(2, 1, 7), piece.right(), Plan::Loot::Library}};
    if (piece.variant == 1) {
        chests.append({piece.at(11, 6, 12), piece.left(), Plan::Loot::Library});
    }
    return chests;
}

std::optional<QcBlockPos> QcStrongholdRooms::spawnerOf(const Piece &piece)
{
    return piece.kind == Kind::PortalRoom ? std::optional(piece.at(5, frameY, 6)) : std::nullopt;
}

QList<QcStrongholdPlan::Frame> QcStrongholdRooms::framesOf(const Piece &piece, quint64 seed, double eyeChance)
{
    QList<Plan::Frame> frames;
    if (piece.kind != Kind::PortalRoom) {
        return frames;
    }
    // Round the 3 × 3 middle at x 4 to 6 and z 9 to 11, each facing it.
    const auto add = [&](int x, int z, Side facing) {
        const QcBlockPos at = piece.at(x, frameY, z);
        frames.append({at, facing, qcPositionRandom(seed, at.x, at.y, at.z) < eyeChance});
    };
    for (int i = 4; i <= 6; ++i) {
        add(i, 8, piece.facing);
        add(i, 12, back(piece));
    }
    for (int i = 9; i <= 11; ++i) {
        add(3, i, piece.right());
        add(7, i, piece.left());
    }
    return frames;
}

void QcStrongholdRooms::furnish(QcStrongholdCanvas &canvas, const Piece &piece)
{
    switch (piece.kind) {
    case Kind::Prison:
        furnishPrison(canvas, piece);
        break;
    case Kind::Crossing:
        furnishCrossing(canvas, piece);
        break;
    case Kind::Library:
        furnishLibrary(canvas, piece);
        break;
    case Kind::PortalRoom:
        furnishPortalRoom(canvas, piece);
        break;
    case Kind::Start:
    case Kind::Corridor:
    case Kind::ChestCorridor:
    case Kind::LeftTurn:
    case Kind::RightTurn:
    case Kind::StraightStairs:
    case Kind::SpiralStairs:
        break;
    }
    const QList<Plan::Chest> chests = chestsOf(piece);
    for (const Plan::Chest &chest : chests) {
        canvas.set(chest.at, Blocks::of(canvas.blocks().chests, chest.facing));
    }
    if (const std::optional<QcBlockPos> spawner = spawnerOf(piece)) {
        canvas.set(*spawner, canvas.blocks().spawner);
    }
}
