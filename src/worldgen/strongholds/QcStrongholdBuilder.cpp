// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/strongholds/QcStrongholdBuilder.h"

#include "worldgen/strongholds/QcStrongholdCanvas.h"
#include "worldgen/strongholds/QcStrongholdRooms.h"

#include <array>
#include <utility>

namespace {

using Plan = QcStrongholdPlan;
using Piece = Plan::Piece;
using Kind = Plan::Kind;
using Blocks = QcStrongholdBlocks;

// The cells round the middle pillar of a spiral staircase in the order one walks down them
// (x, z), starting under the doorway in and ending before the doorway out: a turn and a half.
constexpr std::array<std::pair<int, int>, 13> spiralSteps{
    {{2, 1}, {3, 1}, {3, 2}, {3, 3}, {2, 3}, {1, 3}, {1, 2}, {1, 1}, {2, 1}, {3, 1}, {3, 2}, {3, 3}, {2, 3}}};
// The height one stands at on the first step of a staircase, over its floor.
constexpr int stairsTop = 7;

} // namespace

QcStrongholdBuilder::QcStrongholdBuilder(const QcStrongholdBlocks &blocks, const QcStrongholdSettings &settings, quint64 seed)
    : m_blocks(blocks)
    , m_settings(settings)
    , m_seed(seed)
{
}

void QcStrongholdBuilder::build(QcChunk &chunk, const QcStrongholdPlan &plan) const
{
    QcStrongholdCanvas canvas(chunk, m_blocks, m_settings, m_seed);
    for (const Piece &piece : plan.pieces) {
        if (canvas.reaches(piece.box)) {
            buildPiece(canvas, piece);
        }
    }
    for (const Plan::Frame &frame : plan.frames) {
        canvas.set(frame.at, Blocks::of(frame.eye ? m_blocks.framesWithEye : m_blocks.frames, frame.facing));
    }
}

void QcStrongholdBuilder::buildPiece(QcStrongholdCanvas &canvas, const Piece &piece)
{
    const Plan::Shape shape = piece.shape();
    canvas.wall(piece.box);
    canvas.fill(piece, {1, 1, 1}, {shape.width - 2, shape.height - 2, shape.depth - 2}, qcAirBlockId);
    if (piece.kind != Kind::Start) {
        doorway(canvas, piece);
    }
    exits(canvas, piece);
    if (piece.kind == Kind::Start || piece.kind == Kind::SpiralStairs) {
        spiral(canvas, piece);
    } else if (piece.kind == Kind::StraightStairs) {
        flight(canvas, piece);
    }
    QcStrongholdRooms::furnish(canvas, piece);
}

void QcStrongholdBuilder::doorway(QcStrongholdCanvas &canvas, const Piece &piece)
{
    const Blocks &blocks = canvas.blocks();
    const Plan::Shape shape = piece.shape();
    const int x = shape.doorX;
    const int y = shape.doorY;
    const QcBlockPos low{x - 1, y, 0};
    const QcBlockPos high{x + 1, y + 2, 0};
    switch (piece.door) {
    case Plan::Door::Opening:
        canvas.fill(piece, low, high, qcAirBlockId);
        break;
    case Plan::Door::WoodDoor:
        canvas.put(piece, {x, y, 0}, Blocks::of(blocks.woodDoorLower, piece.facing));
        canvas.put(piece, {x, y + 1, 0}, Blocks::of(blocks.woodDoorUpper, piece.facing));
        break;
    case Plan::Door::Grates:
        canvas.fill(piece, low, high, blocks.barsAcross(piece.facing));
        canvas.fill(piece, {x, y, 0}, {x, y + 1, 0}, qcAirBlockId);
        break;
    case Plan::Door::IronDoor:
        canvas.put(piece, {x, y, 0}, Blocks::of(blocks.ironDoorLower, piece.facing));
        canvas.put(piece, {x, y + 1, 0}, Blocks::of(blocks.ironDoorUpper, piece.facing));
        // A button on the wall beside it, inside and outside.
        canvas.put(piece, {x + 1, y + 1, 1}, Blocks::of(blocks.buttons, QcFortressPlan::opposite(piece.facing)));
        canvas.put(piece, {x + 1, y + 1, -1}, Blocks::of(blocks.buttons, piece.facing));
        break;
    }
}

void QcStrongholdBuilder::exits(QcStrongholdCanvas &canvas, const Piece &piece)
{
    const Plan::Shape shape = piece.shape();
    for (int i = 0; i < shape.exitCount; ++i) {
        const Plan::Doorway &doorway = shape.exits[static_cast<std::size_t>(i)];
        if (!piece.opens(doorway.exit)) {
            continue;
        }
        const bool ahead = doorway.exit == Plan::Exit::Forward;
        const QcBlockPos low{doorway.x - (ahead ? 1 : 0), doorway.y, doorway.z - (ahead ? 0 : 1)};
        const QcBlockPos high{doorway.x + (ahead ? 1 : 0), doorway.y + 2, doorway.z + (ahead ? 0 : 1)};
        canvas.fill(piece, low, high, qcAirBlockId);
    }
}

void QcStrongholdBuilder::spiral(QcStrongholdCanvas &canvas, const Piece &piece)
{
    // Half a block down with every cell: a bottom slab on the half heights, bricks on the whole.
    const Blocks &blocks = canvas.blocks();
    canvas.wall(piece, {2, 1, 2}, {2, piece.shape().height - 2, 2});
    for (std::size_t step = 0; step < spiralSteps.size(); ++step) {
        const auto [x, z] = spiralSteps[step];
        const int halves = 2 * stairsTop - static_cast<int>(step);
        if (halves % 2 == 0) {
            const QcBlockPos cell = piece.at(x, halves / 2 - 1, z);
            canvas.set(cell, canvas.brickAt(cell));
        } else {
            canvas.put(piece, {x, halves / 2, z}, blocks.slab);
        }
    }
}

void QcStrongholdBuilder::flight(QcStrongholdCanvas &canvas, const Piece &piece)
{
    // A step down with every cell from the doorway in to the doorway out, bricks under them.
    const Blocks &blocks = canvas.blocks();
    for (int z = 1; z < stairsTop; ++z) {
        const int top = stairsTop - z;
        if (top > 1) {
            canvas.wall(piece, {1, 1, z}, {3, top - 1, z});
        }
        canvas.fill(piece, {1, top, z}, {3, top, z}, Blocks::of(blocks.stairs, QcFortressPlan::opposite(piece.facing)));
    }
}
