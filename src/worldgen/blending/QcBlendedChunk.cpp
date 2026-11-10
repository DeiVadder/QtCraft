// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/blending/QcBlendedChunk.h"

#include "worldgen/generators/QcWorldGenerator.h"

#include <algorithm>
#include <limits>

namespace {

// The top layers of the ground that come from the version the patch chooses.
constexpr int surfaceLayers = 4;
// A crown belongs to a trunk at most this far away.
constexpr int crownReach = 10;

} // namespace

QcBlendedChunk::QcBlendedChunk(Versions versions, const QcBlendSources &sources, const QcBlendGrid &grid, const QcWorldGenerator &generator)
    : m_versions(versions)
    , m_pos(versions.older.pos())
    , m_sources(sources)
    , m_grid(grid)
    , m_generator(generator)
    , m_source(*sources.constFind(m_pos))
{
}

QcBlendedChunk::Side QcBlendedChunk::side(bool newer) const
{
    return newer ? Side{&m_versions.newer, m_source.newerShape.get(), true} : Side{&m_versions.older, m_source.olderShape.get(), false};
}

QcBlockId QcBlendedChunk::fillerOf(const Side &side, int x, int z) const
{
    for (int y = side.shape->column(x, z).ground - surfaceLayers; y > 0; --y) {
        const QcBlockId block = side.chunk->block(x, y, z);
        if (m_generator.isNaturalGround(block)) {
            return block;
        }
    }
    return side.chunk->block(x, side.shape->column(x, z).ground, z);
}

void QcBlendedChunk::writeGround(QcChunk &out, int x, int z, const QcBlendGrid::Cell &cell) const
{
    const Side under = side(cell.share >= 0.5F);
    const Side top = side(cell.newerSurface);
    const int underGround = under.shape->column(x, z).ground;
    const int topGround = top.shape->column(x, z).ground;
    const QcBlockId filler = fillerOf(under, x, z);

    const int layers = cell.height - surfaceLayers;
    for (int y = 0; y <= layers; ++y) {
        const QcBlockId block = y <= underGround - surfaceLayers ? under.chunk->block(x, y, z) : filler;
        out.setBlock(x, y, z, block == qcAirBlockId && y >= cell.sealedFrom ? filler : block);
    }
    for (int y = std::max(layers + 1, 0); y <= cell.height; ++y) {
        const QcBlockId block = top.chunk->block(x, topGround - (cell.height - y), z);
        out.setBlock(x, y, z, m_generator.isNaturalGround(block) ? block : filler);
    }

    for (int y = cell.height + 1; y <= cell.water; ++y) {
        out.setBlock(x, y, z, m_generator.waterBlock());
    }
    // Water or the ice on it, as the chosen version has it.
    if (const int wetTop = top.shape->column(x, z).water; cell.water >= 0 && wetTop >= 0) {
        out.setBlock(x, cell.water, z, top.chunk->block(x, wetTop, z));
    }
}

QcBlendedChunk::Move QcBlendedChunk::ownMove(const Side &side, QcColumnShape shape, const QcBlendGrid::Cell &cell) const
{
    return {cell.newerSurface == side.newer, cell.height - shape.ground};
}

std::optional<QcChunkShape::Root> QcBlendedChunk::nearestRoot(const Side &side, int x, int z) const
{
    std::optional<QcChunkShape::Root> nearest;
    int best = std::numeric_limits<int>::max();
    for (int around = 0; around < 9; ++around) {
        const auto source = m_sources.constFind(QcChunkPos{m_pos.x - 1 + around % 3, m_pos.z - 1 + around / 3});
        if (source == m_sources.cend()) {
            continue;
        }
        const QList<QcChunkShape::Root> &roots = (side.newer ? source->newerShape : source->olderShape)->roots();
        for (const QcChunkShape::Root &root : roots) {
            const int distance = (root.x - x) * (root.x - x) + (root.z - z) * (root.z - z);
            if (distance < best && distance <= crownReach * crownReach) {
                best = distance;
                nearest = root;
            }
        }
    }
    return nearest;
}

QcBlendedChunk::Move QcBlendedChunk::crownMove(const Side &side, int x, int z) const
{
    const QcBlendGrid::Cell &own = *m_grid.cell(worldX(x), worldZ(z));
    const QcColumnShape &shape = side.shape->column(x, z);
    const std::optional<QcChunkShape::Root> root = nearestRoot(side, worldX(x), worldZ(z));
    if (!root) {
        return ownMove(side, shape, own);
    }
    const QcBlendGrid::Cell *trunk = m_grid.cell(root->x, root->z);
    if (trunk == nullptr) {
        return ownMove(side, shape, own);
    }
    // A tree of dry land does not stand in the water the blend put there.
    return {trunk->newerSurface == side.newer && trunk->water < 0, trunk->height - root->ground};
}

void QcBlendedChunk::writeStanding(QcChunk &out, const Side &side, int x, int z) const
{
    const QcBlendGrid::Cell &cell = *m_grid.cell(worldX(x), worldZ(z));
    const QcColumnShape &shape = side.shape->column(x, z);
    const Move own = ownMove(side, shape, cell);
    std::optional<Move> crown;
    const int top = side.chunk->highestNonAir(x, z);
    for (int y = shape.ground + 1; y <= top; ++y) {
        const QcBlockId block = side.chunk->block(x, y, z);
        if (block == qcAirBlockId || m_generator.isWaterBlock(block)) {
            continue;
        }
        const bool standing = y <= shape.ground + shape.stack;
        if (!standing && y == shape.water + 1) {
            // On the water: a lily pad goes where the water goes.
            if (own.kept && cell.water >= 0) {
                out.setBlock(x, cell.water + 1, z, block);
            }
            continue;
        }
        const bool byTrunk = shape.isRoot() || !standing;
        if (byTrunk && !crown) {
            crown = crownMove(side, x, z);
        }
        const Move move = byTrunk ? *crown : own;
        const int target = y + move.shift;
        if (!move.kept || target <= cell.height || target >= QcWorld::worldHeight) {
            continue;
        }
        // Water plants stay in water, the others out of it.
        const bool wasInWater = y <= shape.water;
        const bool isInWater = target <= cell.water;
        if (wasInWater == isInWater) {
            out.setBlock(x, target, z, block);
        }
    }
}

void QcBlendedChunk::writeWhole(QcChunk &out, int x, int z, const QcBlendGrid::Cell &cell) const
{
    const Side source = side(cell.newerSurface);
    const int top = source.chunk->highestNonAir(x, z);
    for (int y = 0; y <= top; ++y) {
        out.setBlock(x, y, z, source.chunk->block(x, y, z));
    }
    const int sealedTop = std::min(cell.sealedTo, cell.height);
    for (int y = cell.sealedFrom; y <= sealedTop; ++y) {
        if (source.chunk->block(x, y, z) == qcAirBlockId) {
            out.setBlock(x, y, z, fillerOf(source, x, z));
        }
    }
}

std::shared_ptr<QcChunk> QcBlendedChunk::build() const
{
    auto out = std::make_shared<QcChunk>(m_pos);
    for (int z = 0; z < QcWorld::chunkSize; ++z) {
        for (int x = 0; x < QcWorld::chunkSize; ++x) {
            const QcBlendGrid::Cell &cell = *m_grid.cell(worldX(x), worldZ(z));
            if (cell.whole) {
                writeWhole(*out, x, z, cell);
                continue;
            }
            writeGround(*out, x, z, cell);
            writeStanding(*out, side(true), x, z);
            writeStanding(*out, side(false), x, z);
        }
    }
    return out;
}
