// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/bastion/QcBastionBuilder.h"

namespace {

using Plan = QcBastionPlan;
using Paint = QcBastionPlan::Paint;

// The shares of the bricks that are cracked and gilded.
constexpr double crackedShare = 0.12;
constexpr double gildedShare = 0.015;
// The share of the cells that fall away at the bottom of a crumbling box, and what its top adds.
constexpr double crumbleShare = 0.1;
constexpr double crumbleGrowth = 0.6;

} // namespace

QcBastionBuilder::QcBastionBuilder(const QcBastionBlocks &blocks, const QcNetherDefinitions &definitions, quint64 seed)
    : m_blocks(blocks)
    , m_definitions(definitions)
    , m_seed(seed)
{
}

QcBlockId QcBastionBuilder::blockOf(const Plan::Stroke &stroke) const
{
    const auto index = static_cast<std::size_t>(stroke.side);
    switch (stroke.paint) {
    case Paint::Rough:
        return blocks().rough;
    case Paint::Polished:
        return blocks().polished;
    case Paint::Chiseled:
        return blocks().chiseled;
    case Paint::Gilded:
        return blocks().gilded;
    case Paint::Gold:
        return blocks().gold;
    case Paint::Soil:
        return blocks().soil;
    case Paint::Lava:
        return blocks().lava;
    case Paint::Wall:
        return blocks().wallAlong(stroke.side);
    case Paint::Stairs:
        return blocks().stairs[index];
    case Paint::Lantern:
        return blocks().lantern;
    case Paint::Air:
    case Paint::Brick:
    case Paint::Foundation:
    case Paint::Crumble:
        break;
    }
    return qcAirBlockId;
}

void QcBastionBuilder::brick(QcChunkCanvas &canvas, const QcBlockBox &box) const
{
    const QcBlockBox cells = canvas.clipped(box);
    for (int y = cells.low.y; y <= cells.high.y; ++y) {
        for (int z = cells.low.z; z <= cells.high.z; ++z) {
            for (int x = cells.low.x; x <= cells.high.x; ++x) {
                const double draw = canvas.drawAt({x, y, z});
                const QcBlockId worn = draw < gildedShare ? blocks().gilded : blocks().cracked;
                canvas.set({x, y, z}, draw < gildedShare + crackedShare ? worn : blocks().brick);
            }
        }
    }
}

void QcBastionBuilder::foundation(QcChunkCanvas &canvas, const QcBlockBox &box) const
{
    const QcBlockBox cells = canvas.clipped(box);
    for (int z = cells.low.z; z <= cells.high.z; ++z) {
        for (int x = cells.low.x; x <= cells.high.x; ++x) {
            for (int y = cells.high.y; y > 0 && blocks().isOpen(canvas.at({x, y, z})); --y) {
                canvas.set({x, y, z}, blocks().rough);
            }
        }
    }
}

void QcBastionBuilder::crumble(QcChunkCanvas &canvas, const QcBlockBox &box) const
{
    const QcBlockBox cells = canvas.clipped(box);
    const double height = std::max(1, box.high.y - box.low.y);
    for (int y = cells.low.y; y <= cells.high.y; ++y) {
        const double share = crumbleShare + crumbleGrowth * (y - box.low.y) / height;
        for (int z = cells.low.z; z <= cells.high.z; ++z) {
            crumbleRow(canvas, {{cells.low.x, y, z}, {cells.high.x, y, z}}, share);
        }
    }
}

void QcBastionBuilder::crumbleRow(QcChunkCanvas &canvas, const QcBlockBox &row, double share) const
{
    for (int x = row.low.x; x <= row.high.x; ++x) {
        const QcBlockPos cell{x, row.low.y, row.low.z};
        if (!blocks().isOpen(canvas.at(cell)) && canvas.drawAt(cell) < share) {
            canvas.set(cell, qcAirBlockId);
        }
    }
}

void QcBastionBuilder::paint(QcChunkCanvas &canvas, const Plan::Stroke &stroke) const
{
    switch (stroke.paint) {
    case Paint::Brick:
        brick(canvas, stroke.box);
        break;
    case Paint::Foundation:
        foundation(canvas, stroke.box);
        break;
    case Paint::Crumble:
        crumble(canvas, stroke.box);
        break;
    default:
        canvas.fill(stroke.box, blockOf(stroke));
        break;
    }
}

void QcBastionBuilder::build(QcChunk &chunk, const QcBastionPlan &plan) const
{
    for (const QcBlockBox &part : plan.parts) {
        m_definitions.sealPools(chunk, {part.low.offset(-1, 0, -1), part.high.offset(1, 1, 1)});
    }
    QcChunkCanvas canvas(chunk, m_seed);
    for (const Plan::Stroke &stroke : plan.strokes) {
        if (canvas.reaches(stroke.box)) {
            paint(canvas, stroke);
        }
    }
    for (const Plan::Chest &chest : plan.chests) {
        canvas.set(chest.at, blocks().chests[static_cast<std::size_t>(chest.facing)]);
    }
    for (const QcBlockPos &spawner : plan.spawners) {
        canvas.set(spawner, blocks().spawner);
    }
}
