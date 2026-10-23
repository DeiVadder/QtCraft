#include "worldgen/terrain/QcPoolPlacer.h"

#include "worldgen/terrain/QcNoise.h"

#include <algorithm>
#include <cmath>

namespace {

int cellKey(int x, int y, int z)
{
    return (y * QcWorld::chunkSize + z) * QcWorld::chunkSize + x;
}

QcBlockPos cellPos(int cell)
{
    return {cell % QcWorld::chunkSize, cell / (QcWorld::chunkSize * QcWorld::chunkSize), cell / QcWorld::chunkSize % QcWorld::chunkSize};
}

} // namespace

QcPoolPlacer::QcPoolPlacer(std::shared_ptr<const QcContentRegistry> content, const QcTerrainDefinitions &definitions)
    : m_content(std::move(content))
    , m_pools(definitions.pools)
    , m_bedrock(definitions.bedrock)
{
}

bool QcPoolPlacer::holdsLiquid(QcBlockId block) const
{
    if (block == qcAirBlockId || !m_content->isValidBlock(block)) {
        return false;
    }

    const QcBlockDefinition &definition = m_content->block(block);
    return definition.solid && !definition.liquid && !definition.replaceable;
}

std::optional<int> QcPoolPlacer::caveFloor(const QcChunk &chunk, QcBlockPos start, int minY) const
{
    for (int y = start.y; y >= minY; --y) {
        if (chunk.block(start.x, y, start.z) == qcAirBlockId && holdsLiquid(chunk.block(start.x, y - 1, start.z))) {
            return y;
        }
    }
    return std::nullopt;
}

void QcPoolPlacer::placeInCaves(QcChunk &chunk) const
{
    const QcChunkPos pos = chunk.pos();
    for (const Pool &pool : m_pools) {
        for (int attempt = 0; attempt < pool.attempts && !pool.surface; ++attempt) {
            const auto random = [&](int salt) { return qcPositionRandom(pool.seed, pos.x, attempt * 8 + salt, pos.z); };
            if (random(0) >= pool.chance) {
                continue;
            }

            // Cells on the chunk's edge never become liquid (their neighbours lie in other chunks).
            const int x = 1 + static_cast<int>(random(1) * (QcWorld::chunkSize - 2));
            const int z = 1 + static_cast<int>(random(2) * (QcWorld::chunkSize - 2));
            const int startY = pool.minY + static_cast<int>(random(3) * (pool.maxY - pool.minY + 1));
            if (const std::optional<int> floorY = caveFloor(chunk, {x, startY, z}, pool.minY)) {
                sink(chunk, pool, {x, *floorY, z}, attempt);
            }
        }
    }
}

void QcPoolPlacer::sink(QcChunk &chunk, const Pool &pool, QcBlockPos floor, int attempt) const
{
    const QSet<int> cells = contained(chunk, bowl(chunk, pool, floor, attempt));
    if (!opensUpwards(chunk, cells)) {
        return;
    }
    fill(chunk, pool, cells, floor.y);
}

QSet<int> QcPoolPlacer::bowl(const QcChunk &chunk, const Pool &pool, QcBlockPos floor, int attempt) const
{
    const int baseX = chunk.pos().x * QcWorld::chunkSize;
    const int baseZ = chunk.pos().z * QcWorld::chunkSize;
    const double radius = pool.radius - 0.6 * qcPositionRandom(pool.seed, baseX + floor.x, floor.y + attempt, baseZ + floor.z);
    QSet<int> cells;
    // Columns within the radius, one block off the chunk's edges.
    constexpr int last = QcWorld::chunkSize - 1;
    const int x0 = std::max(1, floor.x - pool.radius);
    const int z0 = std::max(1, floor.z - pool.radius);
    const int width = std::min(last - 1, floor.x + pool.radius) - x0 + 1;
    const int length = std::min(last - 1, floor.z + pool.radius) - z0 + 1;
    if (width <= 0 || length <= 0) {
        return cells;
    }
    for (int layer = 0; layer < pool.depth; ++layer) {
        const BowlLayer at{{floor.x, floor.y - 1 - layer, floor.z}, layer, radius - layer * 0.9};
        for (int column = 0; column < width * length; ++column) {
            const int x = x0 + column % width;
            const int z = z0 + column / width;
            if (inBowl(chunk, pool, at, x, z)) {
                cells.insert(cellKey(x, at.centre.y, z));
            }
        }
    }
    return cells;
}

bool QcPoolPlacer::inBowl(const QcChunk &chunk, const Pool &pool, const BowlLayer &layer, int x, int z) const
{
    const int baseX = chunk.pos().x * QcWorld::chunkSize;
    const int baseZ = chunk.pos().z * QcWorld::chunkSize;
    const auto [centreX, y, centreZ] = layer.centre;
    const double distance = std::hypot(x - centreX, z - centreZ) + 0.8 * qcPositionRandom(pool.seed, baseX + x, y, baseZ + z);
    const QcBlockId block = chunk.block(x, y, z);
    const bool fits = block == qcAirBlockId || holdsLiquid(block);
    return distance <= layer.radius + 0.4 && block != m_bedrock && fits;
}

QSet<int> QcPoolPlacer::contained(const QcChunk &chunk, QSet<int> cells) const
{
    const auto bounded = [&](int x, int y, int z) { return cells.contains(cellKey(x, y, z)) || holdsLiquid(chunk.block(x, y, z)); };
    for (bool changed = true; changed;) {
        changed = false;
        for (auto it = cells.begin(); it != cells.end();) {
            const auto [x, y, z] = cellPos(*it);
            // Another liquid above (an earlier pool) would meet this one.
            const bool liquidAbove = !cells.contains(cellKey(x, y + 1, z)) && isLiquid(chunk.block(x, y + 1, z));
            if (bounded(x - 1, y, z) && bounded(x + 1, y, z) && bounded(x, y, z - 1) && bounded(x, y, z + 1)
                && bounded(x, y - 1, z) && !liquidAbove) {
                ++it;
                continue;
            }
            it = cells.erase(it);
            changed = true;
        }
    }
    return cells;
}

bool QcPoolPlacer::opensUpwards(const QcChunk &chunk, const QSet<int> &cells) const
{
    return std::ranges::any_of(cells, [&](int cell) {
        const auto [x, y, z] = cellPos(cell);
        return chunk.block(x, y + 1, z) == qcAirBlockId && !cells.contains(cellKey(x, y + 1, z));
    });
}

void QcPoolPlacer::fill(QcChunk &chunk, const Pool &pool, const QSet<int> &cells, int floorY) const
{
    for (const int cell : cells) {
        const auto [x, y, z] = cellPos(cell);
        chunk.setBlock(x, y, z, pool.liquid);
        if (y == floorY - 1) {
            hollowAbove(chunk, {x, y, z}, floorY);
        }
    }
}

void QcPoolPlacer::hollowAbove(QcChunk &chunk, QcBlockPos cell, int floorY) const
{
    // Where the cave floor beside the start is higher, the rock over the pool is hollowed out up
    // to two blocks above the start's floor, so the pool lies open instead of under a ledge. Rock
    // that holds back another liquid (beside or above it) or carries a falling block stays.
    const int x = cell.x;
    const int z = cell.z;

    for (int above = floorY; above <= floorY + 1; ++above) {
        const QcBlockId block = chunk.block(x, above, z);
        const QcBlockId top = chunk.block(x, above + 1, z);
        const bool holdsSomething = isLiquid(top) || (m_content->isValidBlock(top) && m_content->block(top).falls)
                                    || isLiquid(chunk.block(x - 1, above, z)) || isLiquid(chunk.block(x + 1, above, z))
                                    || isLiquid(chunk.block(x, above, z - 1)) || isLiquid(chunk.block(x, above, z + 1));
        if (block != m_bedrock && holdsLiquid(block) && !holdsSomething) {
            chunk.setBlock(x, above, z, qcAirBlockId);
        }
    }
}
