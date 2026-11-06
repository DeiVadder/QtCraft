// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/nether/QcNetherDecorator.h"

#include "worldgen/nether/QcNetherTerrain.h"
#include "worldgen/terrain/QcOreVeins.h"

#include <algorithm>
#include <array>

namespace {

constexpr int lastColumn = QcWorld::chunkSize - 1;
constexpr std::array<std::array<int, 3>, 6> faces{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};

// A value in [0, 1) of the try `attempt` of something with `seed` in the chunk at `pos`.
double chance(quint64 seed, QcChunkPos pos, int attempt, int salt)
{
    return qcPositionRandom(seed, pos.x, attempt * 8 + salt, pos.z);
}

// The whole number from `low` to `high` a roll of `chance` in 0 to 1 stands for.
int pick(double chance, int low, int high)
{
    return low + static_cast<int>(chance * (high - low + 1));
}

// The cells a spring may lie in: the outermost columns lack neighbours to close it in.
bool isInner(QcBlockPos cell)
{
    return cell.x > 0 && cell.x < lastColumn && cell.z > 0 && cell.z < lastColumn;
}

bool isInsideChunk(int x, int z)
{
    return x >= 0 && x <= lastColumn && z >= 0 && z <= lastColumn;
}

// How many cells around `cell` of `chunk` hold `block`, cells of other chunks left out.
int neighboursWith(const QcChunk &chunk, QcBlockPos cell, QcBlockId block)
{
    int count = 0;
    for (const auto &face : faces) {
        const int x = cell.x + face[0];
        const int z = cell.z + face[2];
        count += isInsideChunk(x, z) && chunk.block(x, cell.y + face[1], z) == block ? 1 : 0;
    }
    return count;
}

// Calls `place` with every entry of `entries` and each of its tries.
template <typename Entries, typename Place>
void eachTry(const Entries &entries, Place place)
{
    for (const auto &entry : entries) {
        for (int attempt = 0; attempt < entry.tries; ++attempt) {
            place(entry, attempt);
        }
    }
}

} // namespace

QcNetherDecorator::QcNetherDecorator(const QcNetherDefinitions &definitions, const QcNetherTerrain &terrain)
    : m_definitions(definitions)
    , m_terrain(terrain)
{
}

double QcNetherDecorator::patchRoll(int x, int z) const
{
    return qcPositionRandom(m_definitions.patchSeed, x, 0, z);
}

void QcNetherDecorator::groundColumn(QcChunk &chunk, int localX, int localZ) const
{
    const int worldX = chunk.pos().x * QcWorld::chunkSize + localX;
    const int worldZ = chunk.pos().z * QcWorld::chunkSize + localZ;
    const QcNetherDefinitions::Biome &biome = m_definitions.biomes[m_terrain.biomeAt(worldX, worldZ)];
    if (biome.surface == m_definitions.filler && biome.patches.isEmpty()) {
        return;
    }

    QcBlockId top = biome.surface;
    double roll = patchRoll(worldX, worldZ);
    for (const auto &[block, share] : biome.patches) {
        if (roll < share) {
            top = block;
            break;
        }
        roll -= share;
    }

    for (int y = m_definitions.settings.roof - 1; y > m_definitions.settings.liquidLevel; --y) {
        if (chunk.block(localX, y, localZ) != m_definitions.filler || chunk.block(localX, y + 1, localZ) != qcAirBlockId) {
            continue;
        }
        chunk.setBlock(localX, y, localZ, top);
        for (int depth = 1; depth <= biome.depth && chunk.block(localX, y - depth, localZ) == m_definitions.filler; ++depth) {
            chunk.setBlock(localX, y - depth, localZ, biome.subsurface);
        }
    }
}

void QcNetherDecorator::shoreColumn(QcChunk &chunk, int localX, int localZ) const
{
    const int worldX = chunk.pos().x * QcWorld::chunkSize + localX;
    const int worldZ = chunk.pos().z * QcWorld::chunkSize + localZ;
    const int level = m_definitions.settings.liquidLevel;
    for (const QcNetherDefinitions::Shore &shore : m_definitions.shores) {
        if (shore.noise.fractal2(worldX / shore.scale, worldZ / shore.scale, 2) <= shore.threshold) {
            continue;
        }
        for (int y = level + shore.height; y >= level - shore.height; --y) {
            const QcBlockId above = chunk.block(localX, y + 1, localZ);
            if (chunk.block(localX, y, localZ) == qcAirBlockId || (above != qcAirBlockId && above != m_definitions.liquid)
                || chunk.block(localX, y, localZ) == m_definitions.liquid) {
                continue;
            }
            for (int depth = 0; depth < shore.depth && chunk.block(localX, y - depth, localZ) != m_definitions.bedrock; ++depth) {
                chunk.setBlock(localX, y - depth, localZ, shore.block);
            }
            break;
        }
    }
}

void QcNetherDecorator::placeOres(QcChunk &chunk) const
{
    for (const QcTerrainDefinitions::Ore &ore : m_definitions.ores) {
        for (int vein = 0; vein < ore.veins; ++vein) {
            QcOreVeins::place(chunk, ore, QcOreVeins::grown(ore, vein, chunk.pos()));
        }
    }
}

void QcNetherDecorator::placeCluster(QcChunk &chunk, const QcNetherDefinitions::Cluster &cluster, int attempt) const
{
    const QcChunkPos pos = chunk.pos();
    const int x = pick(chance(cluster.seed, pos, attempt, 0), 0, lastColumn);
    const int z = pick(chance(cluster.seed, pos, attempt, 1), 0, lastColumn);
    int y = pick(chance(cluster.seed, pos, attempt, 2), cluster.minY, cluster.maxY);
    while (y < cluster.maxY && chunk.block(x, y, z) == qcAirBlockId) {
        ++y;
    }
    if (chunk.block(x, y, z) != cluster.on || chunk.block(x, y - 1, z) != qcAirBlockId) {
        return;
    }

    // Each block touches exactly one before it, so the cluster branches like a bunch of grapes.
    const QcBlockPos root{x, y - 1, z};
    chunk.setBlock(root.x, root.y, root.z, cluster.block);
    int placed = 1;
    const int tries = cluster.size * 8;
    for (int step = 0; step < tries && placed < cluster.size; ++step) {
        const int salt = attempt * 4096 + step * 4;
        const QcBlockPos cell{root.x + pick(chance(cluster.seed, pos, salt, 3), -cluster.radius, cluster.radius), root.y - pick(chance(cluster.seed, pos, salt, 4), 0, cluster.depth),
                              root.z + pick(chance(cluster.seed, pos, salt, 5), -cluster.radius, cluster.radius)};
        if (isInsideChunk(cell.x, cell.z) && chunk.block(cell.x, cell.y, cell.z) == qcAirBlockId && neighboursWith(chunk, cell, cluster.block) == 1) {
            chunk.setBlock(cell.x, cell.y, cell.z, cluster.block);
            ++placed;
        }
    }
}

QcBlockPos QcNetherDecorator::springStart(const QcChunk &chunk, const QcNetherDefinitions::Spring &spring, int attempt) const
{
    const QcChunkPos pos = chunk.pos();
    return {pick(chance(spring.seed, pos, attempt, 0), 1, lastColumn - 1), pick(chance(spring.seed, pos, attempt, 1), spring.minY, spring.maxY),
            pick(chance(spring.seed, pos, attempt, 2), 1, lastColumn - 1)};
}

QcBlockPos QcNetherDecorator::springSide(const QcChunk &chunk, const QcNetherDefinitions::Spring &spring, int attempt) const
{
    static constexpr std::array<QcBlockPos, 4> sides{{{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}}};
    return sides[static_cast<std::size_t>(pick(chance(spring.seed, chunk.pos(), attempt, 3), 0, 3))];
}

bool QcNetherDecorator::fallsIntoTheSea(const QcChunk &chunk, QcBlockPos cell) const
{
    int y = cell.y;
    while (y > 0 && chunk.block(cell.x, y, cell.z) == qcAirBlockId) {
        --y;
    }
    return chunk.block(cell.x, y, cell.z) == m_definitions.liquid;
}

void QcNetherDecorator::placeSpring(QcChunk &chunk, const QcNetherDefinitions::Spring &spring, int attempt) const
{
    // Along one side to the first cell before air: a spring in the wall of a cave, not in a cave's
    // middle.
    QcBlockPos cell = springStart(chunk, spring, attempt);
    if (chunk.block(cell.x, cell.y, cell.z) != spring.in) {
        return;
    }
    const QcBlockPos side = springSide(chunk, spring, attempt);
    QcBlockPos next = cell.offset(side.x, 0, side.z);
    while (isInner(next) && chunk.block(next.x, next.y, next.z) == spring.in) {
        cell = next;
        next = cell.offset(side.x, 0, side.z);
    }
    if (chunk.block(next.x, next.y, next.z) != qcAirBlockId || neighboursWith(chunk, cell, spring.in) != 5 || !fallsIntoTheSea(chunk, next)) {
        return;
    }
    chunk.setBlock(cell.x, cell.y, cell.z, spring.block);
}

void QcNetherDecorator::placeFire(QcChunk &chunk, const QcNetherDefinitions::Fire &fire, int attempt) const
{
    const QcChunkPos pos = chunk.pos();
    const int x = pick(chance(fire.seed, pos, attempt, 0), 0, lastColumn);
    const int z = pick(chance(fire.seed, pos, attempt, 1), 0, lastColumn);
    const int level = m_definitions.settings.liquidLevel;
    for (int y = pick(chance(fire.seed, pos, attempt, 2), level + 2, m_definitions.settings.roof - 2); y > level; --y) {
        const QcBlockId floor = chunk.block(x, y - 1, z);
        if (chunk.block(x, y, z) == qcAirBlockId && floor != qcAirBlockId) {
            if (fire.on.contains(floor)) {
                chunk.setBlock(x, y, z, fire.block);
            }
            return;
        }
    }
}

void QcNetherDecorator::decorate(QcChunk &chunk) const
{
    for (int z = 0; z < QcWorld::chunkSize; ++z) {
        for (int x = 0; x < QcWorld::chunkSize; ++x) {
            groundColumn(chunk, x, z);
            shoreColumn(chunk, x, z);
        }
    }
    placeOres(chunk);
    eachTry(m_definitions.clusters, [&](const auto &cluster, int attempt) { placeCluster(chunk, cluster, attempt); });
    eachTry(m_definitions.fires, [&](const auto &fire, int attempt) { placeFire(chunk, fire, attempt); });
}

void QcNetherDecorator::placeSprings(QcChunk &chunk) const
{
    eachTry(m_definitions.springs, [&](const auto &spring, int attempt) { placeSpring(chunk, spring, attempt); });
}

std::optional<QcBlockPos> QcNetherDecorator::springOf(const QcChunk &chunk, const QcNetherDefinitions::Spring &spring, int attempt) const
{
    QcBlockPos cell = springStart(chunk, spring, attempt);
    const QcBlockPos side = springSide(chunk, spring, attempt);
    while (isInner(cell) && chunk.block(cell.x, cell.y, cell.z) == spring.in) {
        cell = cell.offset(side.x, 0, side.z);
    }
    if (chunk.block(cell.x, cell.y, cell.z) != spring.block) {
        return std::nullopt;
    }
    return QcBlockPos{chunk.pos().x * QcWorld::chunkSize + cell.x, cell.y, chunk.pos().z * QcWorld::chunkSize + cell.z};
}

QList<QcBlockPos> QcNetherDecorator::springsIn(const QcChunk &chunk) const
{
    QList<QcBlockPos> springs;
    for (const QcNetherDefinitions::Spring &spring : m_definitions.springs) {
        for (int attempt = 0; attempt < spring.tries; ++attempt) {
            const std::optional<QcBlockPos> found = springOf(chunk, spring, attempt);
            if (found && !springs.contains(*found)) {
                springs.append(*found);
            }
        }
    }
    return springs;
}
