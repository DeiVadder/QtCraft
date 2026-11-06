// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/fortress/QcFortressBuilder.h"

#include "worldgen/fortress/QcFortressRooms.h"

namespace {

using Plan = QcFortressPlan;
using Room = QcFortressPlan::Room;
using Passage = QcFortressPlan::Passage;

// How often a rib, an opening and a light come along a closed passage, and where in that stretch.
constexpr int ribEvery = 6;
constexpr int ribAt = 3;
constexpr int lightEvery = 12;
// Openings alternate between a window behind a fence and an open side opening; each is three
// cells long and two high, over a sill one cell high.
constexpr int openingEvery = 12;
constexpr int windowAt = 0;
constexpr int sideOpeningAt = 6;
constexpr int openingReach = 1;
constexpr int sillHeight = 1;
constexpr int openingHeight = 2;
// The cells of a doorway: lower than the passage, so a lintel stays.
constexpr int doorHeight = 3;

// A box of the column `cell` from `first` to `last` cells above it.
QcBlockBox column(QcBlockPos cell, int first, int last)
{
    return {cell.offset(0, first, 0), cell.offset(0, last, 0)};
}

QcBlockBox grownForSeal(const QcBlockBox &box)
{
    return {box.low.offset(-1, 0, -1), box.high.offset(1, 1, 1)};
}

} // namespace

QcFortressBuilder::QcFortressBuilder(const QcFortressBlocks &blocks, const QcNetherDefinitions &definitions, quint64 seed)
    : m_blocks(blocks)
    , m_definitions(definitions)
    , m_seed(seed)
{
}

void QcFortressBuilder::seal(QcChunk &chunk, const QcFortressPlan &plan) const
{
    for (const Room &room : plan.rooms) {
        m_definitions.sealPools(chunk, grownForSeal(room.shell()));
    }
    for (const Passage &passage : plan.passages) {
        m_definitions.sealPools(chunk, grownForSeal(passage.shell()));
    }
}

void QcFortressBuilder::buildShell(QcFortressCanvas &canvas, const Room &room)
{
    if (!canvas.reaches(room.shell())) {
        return;
    }
    QcBlockBox box = room.shell();
    box.high.y = room.roofed ? box.high.y : box.low.y;
    canvas.brick(box);
}

void QcFortressBuilder::buildShell(QcFortressCanvas &canvas, const Passage &passage)
{
    if (!canvas.reaches(passage.shell())) {
        return;
    }
    const int bottom = passage.rise > 0 ? -1 : 0;
    const int top = passage.roofed ? Plan::passageHeight + 1 : 0;
    for (int along = 0; along < passage.length; ++along) {
        for (int lateral = -Plan::passageHalf; lateral <= Plan::passageHalf; ++lateral) {
            canvas.brick(column(passage.floorAt(along, lateral), bottom, top));
        }
    }
}

void QcFortressBuilder::carve(QcFortressCanvas &canvas, const Room &room)
{
    const QcBlockBox shell = room.shell();
    if (!canvas.reaches(shell)) {
        return;
    }
    const int inset = room.roofed ? 1 : 0;
    const int top = room.roofed ? shell.high.y - 1 : shell.low.y + Plan::roomHeight;
    canvas.fill({{shell.low.x + inset, shell.low.y + 1, shell.low.z + inset}, {shell.high.x - inset, top, shell.high.z - inset}}, qcAirBlockId);
}

void QcFortressBuilder::carve(QcFortressCanvas &canvas, const Passage &passage)
{
    if (!canvas.reaches({passage.shell().low.offset(-1, 0, -1), passage.shell().high.offset(1, 0, 1)})) {
        return;
    }
    const int reach = passage.roofed ? Plan::passageHalf - 1 : Plan::passageHalf;
    for (int along = 0; along < passage.length; ++along) {
        for (int lateral = -reach; lateral <= reach; ++lateral) {
            canvas.fill(column(passage.floorAt(along, lateral), 1, Plan::passageHeight), qcAirBlockId);
        }
    }
    // The doorways through the walls of the rooms at both ends.
    for (const int along : {-1, passage.length}) {
        for (int lateral = 1 - Plan::passageHalf; lateral < Plan::passageHalf; ++lateral) {
            canvas.fill(column(passage.floorAt(along, lateral), 1, doorHeight), qcAirBlockId);
        }
    }
}

void QcFortressBuilder::dressOpenCell(QcFortressCanvas &canvas, const Passage &passage, int along)
{
    const QcFortressBlocks &blocks = canvas.blocks();
    for (const int lateral : {-Plan::passageHalf, Plan::passageHalf}) {
        const QcBlockPos feet = passage.floorAt(along, lateral).offset(0, 1, 0);
        const bool post = along % ribEvery == 0;
        canvas.set(feet, post ? blocks.chiseled : blocks.brick);
        canvas.set(feet.offset(0, 1, 0), blocks.fenceAlong(passage.side));
    }
}

void QcFortressBuilder::dressClosedCell(QcFortressCanvas &canvas, const Passage &passage, int along)
{
    const QcFortressBlocks &blocks = canvas.blocks();
    if (along % ribEvery == ribAt) {
        for (const int lateral : {-Plan::passageHalf, Plan::passageHalf}) {
            canvas.fill(column(passage.floorAt(along, lateral), 1, Plan::passageHeight), blocks.chiseled);
        }
    }
    if (along % lightEvery == ribAt) {
        canvas.set(passage.floorAt(along, 0).offset(0, Plan::passageHeight + 1, 0), blocks.light);
    }
    if (passage.rise > 0) {
        return;
    }
    // The cell is part of a window or a side opening when one is centred within reach of it.
    for (int centre = along - openingReach; centre <= along + openingReach; ++centre) {
        const int phase = ((centre % openingEvery) + openingEvery) % openingEvery;
        if (phase == windowAt || phase == sideOpeningAt) {
            cutOpening(canvas, passage, along, phase == windowAt ? blocks.fenceAlong(passage.side) : qcAirBlockId);
            return;
        }
    }
}

void QcFortressBuilder::cutOpening(QcFortressCanvas &canvas, const Passage &passage, int along, QcBlockId fill)
{
    // Only where the wall stands in open air: never towards rock or a liquid. Only the chunk's own
    // cells decide, cell by cell, so an opening on the chunk's edge may come out shorter.
    for (const int side : {-1, 1}) {
        bool free = true;
        for (int row = sillHeight + 1; row <= sillHeight + openingHeight && free; ++row) {
            const QcBlockPos outside = passage.floorAt(along, side * (Plan::passageHalf + 1)).offset(0, row, 0);
            const QcBlockPos wall = passage.floorAt(along, side * Plan::passageHalf).offset(0, row, 0);
            free = canvas.holds(outside) && canvas.holds(wall) && canvas.at(outside) == qcAirBlockId;
        }
        if (free) {
            canvas.fill(column(passage.floorAt(along, side * Plan::passageHalf), sillHeight + 1, sillHeight + openingHeight), fill);
        }
    }
}

void QcFortressBuilder::dress(QcFortressCanvas &canvas, const Passage &passage)
{
    if (!canvas.reaches(passage.shell())) {
        return;
    }
    const QcFortressBlocks &blocks = canvas.blocks();
    for (int along = 0; along < passage.length; ++along) {
        const bool step = passage.climbed(along) > passage.climbed(along - 1);
        for (int lateral = 1 - Plan::passageHalf; lateral < Plan::passageHalf; ++lateral) {
            if (step) {
                canvas.set(passage.floorAt(along, lateral), blocks.stairsTo(passage.side));
            } else if (lateral == 0) {
                canvas.set(passage.floorAt(along, lateral), blocks.lane);
            }
        }
        if (passage.roofed) {
            dressClosedCell(canvas, passage, along);
        } else {
            dressOpenCell(canvas, passage, along);
        }
    }
}

void QcFortressBuilder::placePiers(QcFortressCanvas &canvas, const QcFortressPlan &plan)
{
    const QcFortressBlocks &blocks = canvas.blocks();
    for (const QcBlockPos &top : plan.piers) {
        if (!canvas.holds(top)) {
            continue;
        }
        for (QcBlockPos cell = top; cell.isInsideWorldHeight() && blocks.isOpen(canvas.at(cell)); cell = cell.offset(0, -1, 0)) {
            canvas.brick({cell, cell});
        }
    }
}

void QcFortressBuilder::build(QcChunk &chunk, const QcFortressPlan &plan) const
{
    seal(chunk, plan);
    QcFortressCanvas canvas(chunk, m_blocks, m_seed);
    for (const Room &room : plan.rooms) {
        buildShell(canvas, room);
    }
    for (const Passage &passage : plan.passages) {
        buildShell(canvas, passage);
    }
    for (const Room &room : plan.rooms) {
        carve(canvas, room);
    }
    for (const Passage &passage : plan.passages) {
        carve(canvas, passage);
    }
    for (const Passage &passage : plan.passages) {
        dress(canvas, passage);
    }
    for (const Room &room : plan.rooms) {
        if (canvas.reaches(room.shell())) {
            QcFortressRooms::furnish(canvas, room);
        }
    }
    for (const Plan::Chest &chest : plan.chests) {
        canvas.set(chest.at, m_blocks.chestTo(chest.facing));
    }
    placePiers(canvas, plan);
}
