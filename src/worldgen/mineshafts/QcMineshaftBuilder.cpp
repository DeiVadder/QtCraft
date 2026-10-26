#include "worldgen/mineshafts/QcMineshaftBuilder.h"

#include <algorithm>
#include <array>
#include <cstdlib>

namespace {

bool inChunk(const QcChunk &chunk, QcBlockPos cell)
{
    return cell.chunk() == chunk.pos() && cell.isInsideWorldHeight();
}

QcBlockId blockAt(const QcChunk &chunk, QcBlockPos cell)
{
    return chunk.block(cell.localX(), cell.y, cell.localZ());
}

void setAt(QcChunk &chunk, QcBlockPos cell, QcBlockId block)
{
    chunk.setBlock(cell.localX(), cell.y, cell.localZ(), block);
}

} // namespace

QcMineshaftBuilder::QcMineshaftBuilder(const QcMineshaftBlocks &blocks, int maxPillar)
    : m_blocks(blocks)
    , m_maxPillar(maxPillar)
{
}

void QcMineshaftBuilder::build(QcChunk &chunk, const QcMineshaftPlan &plan) const
{
    const int x0 = chunk.pos().x * QcWorld::chunkSize;
    const int z0 = chunk.pos().z * QcWorld::chunkSize;
    const Area area{x0 - 1, z0 - 1, x0 + QcWorld::chunkSize, z0 + QcWorld::chunkSize};

    placePillars(chunk, plan);
    placeNodeFloors(chunk, plan, area);
    placeCorridorFloors(chunk, plan, area);
    open(chunk, plan, area);
    for (const QcMineshaftPlan::Room &room : plan.rooms) {
        if (room.placement.overlaps(chunk.pos())) {
            room.placement.writeInto(chunk);
        }
    }
    for (const QcMineshaftPlan::Placed &placed : plan.blocks) {
        if (inChunk(chunk, placed.at)) {
            setAt(chunk, placed.at, placed.block);
        }
    }
}

void QcMineshaftBuilder::buildEntrance(QcChunk &chunk, const QcMineshaftPlan::Entrance &entrance) const
{
    // The headframe's box above the ground's top is emptied of whatever grew or pooled there, the
    // ring around it of leaves and cacti, so one steps out onto the ground.
    for (int i = 0; i < 75; ++i) {
        const int dx = i % 5 - 2;
        const int dz = i / 5 % 5 - 2;
        const QcBlockPos cell = entrance.top.offset(dx, 1 + i / 25, dz);
        const bool box = std::abs(dx) <= 1 && std::abs(dz) <= 1;
        if (!box && (!inChunk(chunk, cell) || !m_blocks.isFoliage(blockAt(chunk, cell)))) {
            continue;
        }
        if (inChunk(chunk, cell)) {
            setAt(chunk, cell, qcAirBlockId);
        }
        seal(chunk, cell);
    }

    for (const QcBlockPos &cell : entrance.shaft) {
        if (inChunk(chunk, cell)) {
            setAt(chunk, cell, qcAirBlockId);
        }
        seal(chunk, cell);
    }
    for (const QcBlockPos &cell : entrance.lining) {
        plankIfOpen(chunk, cell);
    }
    for (const QcMineshaftPlan::Placed &placed : entrance.blocks) {
        if (inChunk(chunk, placed.at)) {
            setAt(chunk, placed.at, placed.block);
        }
    }
}

void QcMineshaftBuilder::plankIfOpen(QcChunk &chunk, QcBlockPos cell) const
{
    if (inChunk(chunk, cell) && m_blocks.isOpen(blockAt(chunk, cell))) {
        setAt(chunk, cell, m_blocks.planks);
    }
}

void QcMineshaftBuilder::placePillars(QcChunk &chunk, const QcMineshaftPlan &plan) const
{
    // Down from below an open floor cell while the cells are open; the floor gets its planks next.
    for (const QcBlockPos &floor : plan.pillars) {
        if (!inChunk(chunk, floor) || !m_blocks.isOpen(blockAt(chunk, floor))) {
            continue;
        }
        for (int depth = 1; depth <= m_maxPillar; ++depth) {
            const QcBlockPos cell = floor.offset(0, -depth, 0);
            if (!cell.isInsideWorldHeight() || !m_blocks.isOpen(blockAt(chunk, cell))) {
                break;
            }
            setAt(chunk, cell, m_blocks.post);
        }
    }
}

void QcMineshaftBuilder::placeNodeFloors(QcChunk &chunk, const QcMineshaftPlan &plan, Area area) const
{
    for (const QcMineshaftPlan::Node &node : plan.nodes) {
        const QcBlockPos c = node.centre;
        if (!area.overlaps(c.x - node.half, c.z - node.half, c.x + node.half, c.z + node.half)) {
            continue;
        }
        const int side = 2 * node.half + 1;
        for (int i = 0; i < side * side; ++i) {
            plankIfOpen(chunk, c.offset(i % side - node.half, -1, i / side - node.half));
        }
    }
}

bool QcMineshaftBuilder::near(const QcMineshaftPlan::Corridor &corridor, Area area)
{
    const QcBlockPos first = corridor.cell(0);
    const QcBlockPos last = corridor.cell(corridor.length - 1);
    return area.overlaps(std::min(first.x, last.x) - 1, std::min(first.z, last.z) - 1, std::max(first.x, last.x) + 1, std::max(first.z, last.z) + 1);
}

void QcMineshaftBuilder::placeCorridorFloors(QcChunk &chunk, const QcMineshaftPlan &plan, Area area) const
{
    // Corridors far from the chunk are skipped whole (a large mine shaft has hundreds).
    for (const QcMineshaftPlan::Corridor &corridor : plan.corridors) {
        if (!near(corridor, area)) {
            continue;
        }
        for (int i = 0; i < 3 * corridor.length; ++i) {
            plankIfOpen(chunk, corridor.cell(i / 3, i % 3 - 1).offset(0, -1, 0));
        }
    }
}

void QcMineshaftBuilder::open(QcChunk &chunk, const QcMineshaftPlan &plan, Area area) const
{
    // Nodes and corridors far from the chunk are skipped whole.
    QcMineshaftPlan near;
    for (const QcMineshaftPlan::Node &node : plan.nodes) {
        if (area.overlaps(node.centre.x - node.half, node.centre.z - node.half, node.centre.x + node.half, node.centre.z + node.half)) {
            near.nodes.append(node);
        }
    }
    for (const QcMineshaftPlan::Corridor &corridor : plan.corridors) {
        if (QcMineshaftBuilder::near(corridor, area)) {
            near.corridors.append(corridor);
        }
    }

    // A liquid sealed beside one cell may be opened as another cell later: it ends open.
    near.forEachOpenCell([&](QcBlockPos cell) {
        if (cell.x < area.x0 || cell.x > area.x1 || cell.z < area.z0 || cell.z > area.z1) {
            return;
        }
        if (inChunk(chunk, cell)) {
            setAt(chunk, cell, qcAirBlockId);
        }
        seal(chunk, cell);
    });
}

void QcMineshaftBuilder::seal(QcChunk &chunk, QcBlockPos cell) const
{
    constexpr std::array<std::array<int, 3>, 5> around{{{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}}};
    for (const auto &[dx, dy, dz] : around) {
        const QcBlockPos next = cell.offset(dx, dy, dz);
        if (!inChunk(chunk, next)) {
            continue;
        }
        // Sand and gravel above would fall in once a neighbour changes.
        const QcBlockId block = blockAt(chunk, next);
        if (m_blocks.isLiquid(block) || (dy == 1 && m_blocks.falls(block))) {
            setAt(chunk, next, m_blocks.seal);
        }
    }
}
