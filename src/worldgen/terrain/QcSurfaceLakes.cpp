#include "worldgen/terrain/QcSurfaceLakes.h"

#include "common/core/QcIntMath.h"
#include "worldgen/terrain/QcNoise.h"
#include "worldgen/terrain/QcTerrainColumnGrid.h"
#include "worldgen/terrain/QcTerrainGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {

// Purposes of the random values.
enum Purpose : int {
    Chance = 1,
    ChunkX,
    ChunkZ,
    Centre,
    Shape,
    Level,
    Edge,
};

// Squares whose plans are kept at most; beyond, the cache starts over.
constexpr qsizetype cacheCapacity = 4096;
// The basin reaches at most this far from the chunk's middle, so its rim and the ground cleared
// around it stay inside the chunk.
constexpr double maxReach = 5.5;
// The ground under and around the basin lies within this many blocks of height.
constexpr int maxRise = 3;
// Blocks around the basin whose plants go; the first ring's ground turns to rock.
constexpr int clearRing = 2;
constexpr int chunkSize = QcWorld::chunkSize;

} // namespace

QcSurfaceLakes::QcSurfaceLakes(const QcTerrainGenerator &terrain, const QcTerrainDefinitions &definitions,
                               std::shared_ptr<const QcContentRegistry> content)
    : m_terrain(terrain)
    , m_content(std::move(content))
    , m_pools(definitions.pools)
    , m_soil(definitions.naturalGround)
    , m_stone(definitions.stone)
    , m_seaLevel(definitions.seaLevel)
{
    m_soil.remove(m_stone);
}

double QcSurfaceLakes::random(const Pool &pool, int x, int salt, int z) const
{
    return qcPositionRandom(qcMixSeed(pool.seed, static_cast<quint64>(salt)), x, 0, z);
}

std::optional<QcSurfaceLakes::Site> QcSurfaceLakes::siteIn(QcChunkPos pos) const
{
    for (qsizetype i = 0; i < m_pools.size(); ++i) {
        const Pool &pool = m_pools[i];
        if (!pool.surface || pool.chance <= 0.0) {
            continue;
        }
        std::optional<Site> site = planOf(static_cast<int>(i), qcFloorDiv(pos.x, pool.region), qcFloorDiv(pos.z, pool.region));
        if (site && site->chunk == pos) {
            return site;
        }
    }
    return std::nullopt;
}

std::optional<QcSurfaceLakes::Site> QcSurfaceLakes::planOf(int pool, int regionX, int regionZ) const
{
    const QPair<int, qint64> key{pool, (static_cast<qint64>(regionX) << 32) ^ static_cast<qint64>(static_cast<quint32>(regionZ))};
    {
        const QMutexLocker lock(&m_mutex);
        if (const auto it = m_plans.constFind(key); it != m_plans.cend()) {
            return *it;
        }
    }
    const std::optional<Site> site = plan(pool, regionX, regionZ);
    const QMutexLocker lock(&m_mutex);
    if (m_plans.size() >= cacheCapacity) {
        m_plans.clear();
    }
    return *m_plans.insert(key, site);
}

std::optional<QcSurfaceLakes::Site> QcSurfaceLakes::plan(int pool, int regionX, int regionZ) const
{
    const Pool &lake = m_pools[pool];
    if (random(lake, regionX, Chance, regionZ) >= lake.chance) {
        return std::nullopt;
    }

    // The tried chunks keep `spacing` chunks from those of the next square.
    const int span = lake.region - lake.spacing;
    for (int attempt = 0; attempt < lake.attempts; ++attempt) {
        const int x = regionX * lake.region + static_cast<int>(random(lake, regionX * 8 + attempt, ChunkX, regionZ) * span);
        const int z = regionZ * lake.region + static_cast<int>(random(lake, regionX * 8 + attempt, ChunkZ, regionZ) * span);
        if (std::optional<Site> site = siteAt(pool, {x, z}, attempt)) {
            return site;
        }
    }
    return std::nullopt;
}

QVarLengthArray<QcSurfaceLakes::Blob, 6> QcSurfaceLakes::blobsOf(const Pool &pool, QcChunkPos chunk, int attempt) const
{
    // A few ellipses around the chunk's middle, each within the reach.
    const int baseX = chunk.x * chunkSize;
    const int baseZ = chunk.z * chunkSize;
    const auto draw = [&](int salt) { return random(pool, baseX + attempt, Shape * 64 + salt, baseZ); };
    const double centreX = baseX + 7 + (draw(0) < 0.5 ? 0 : 1);
    const double centreZ = baseZ + 7 + (draw(1) < 0.5 ? 0 : 1);
    const double reach = std::min(static_cast<double>(pool.radius) + 0.5, maxReach);
    const double maxOffset = std::clamp(reach - 2.5, 0.0, 2.0);
    const int count = 3 + static_cast<int>(draw(2) * 3);

    QVarLengthArray<Blob, 6> blobs;
    for (int i = 0; i < count; ++i) {
        const int salt = 4 + i * 6;
        // The first ellipse sits on the middle, so the basin never misses it.
        const double offsetX = i == 0 ? 0.0 : (2.0 * draw(salt) - 1.0) * maxOffset;
        const double offsetZ = i == 0 ? 0.0 : (2.0 * draw(salt + 1) - 1.0) * maxOffset;
        const double roomX = reach - std::abs(offsetX);
        const double roomZ = reach - std::abs(offsetZ);
        Blob blob;
        blob.x = centreX + offsetX;
        blob.z = centreZ + offsetZ;
        blob.radiusX = std::min(2.0, roomX) + draw(salt + 2) * std::max(0.0, roomX - 2.0);
        blob.radiusZ = std::min(2.0, roomZ) + draw(salt + 3) * std::max(0.0, roomZ - 2.0);
        blob.depth = std::max(1, pool.depth - 1) + draw(salt + 4) * std::min(1, pool.depth - 1) + 0.49;
        blobs.append(blob);
    }
    return blobs;
}

int QcSurfaceLakes::depthAt(const Site &site, int x, int z) const
{
    // A little per column on the distance frays the shore.
    const double fray = 0.3 * (random(m_pools[site.pool], x, Edge, z) - 0.5);
    double deepest = 0.0;
    for (const Blob &blob : site.blobs) {
        const double dx = (x - blob.x) / blob.radiusX;
        const double dz = (z - blob.z) / blob.radiusZ;
        const double reach = dx * dx + dz * dz + fray;
        if (reach < 1.0) {
            deepest = std::max(deepest, blob.depth * std::sqrt(1.0 - reach));
        }
    }
    return static_cast<int>(std::lround(deepest));
}

std::optional<QcSurfaceLakes::Site> QcSurfaceLakes::siteAt(int pool, QcChunkPos chunk, int attempt) const
{
    const Pool &lake = m_pools[pool];
    Site site;
    site.pool = pool;
    site.chunk = chunk;
    site.blobs = blobsOf(lake, chunk, attempt);

    const int baseX = chunk.x * chunkSize;
    const int baseZ = chunk.z * chunkSize;
    QList<std::pair<int, int>> basin;
    Rect bounds{baseX + chunkSize, baseZ + chunkSize, baseX - 1, baseZ - 1};
    for (int z = baseZ + 1; z < baseZ + chunkSize - 1; ++z) {
        for (int x = baseX + 1; x < baseX + chunkSize - 1; ++x) {
            if (depthAt(site, x, z) >= 1) {
                basin.append({x, z});
                bounds = {std::min(bounds.x0, x), std::min(bounds.z0, z), std::max(bounds.x1, x), std::max(bounds.z1, z)};
            }
        }
    }
    const std::optional<int> level = basin.isEmpty() ? std::nullopt : levelOver(lake, basin, random(lake, baseX + attempt, Level, baseZ));
    if (!level) {
        return std::nullopt;
    }
    site.level = *level;
    site.footprint = {bounds.x0 - clearRing, bounds.z0 - clearRing, bounds.x1 + clearRing, bounds.z1 + clearRing};
    // Like every structure, a lake keeps its whole footprint off the deltas (decision E-850): the
    // basin and its ring are dry land already, but the corners of the footprint reach further.
    if (m_terrain.touchesDelta(site.footprint)) {
        return std::nullopt;
    }
    return site;
}

std::optional<int> QcSurfaceLakes::levelOver(const Pool &pool, const QList<std::pair<int, int>> &basin, double random) const
{
    // The middle column lies in an allowed biome; the basin and the ring around it are dry, level,
    // plain land: no water, river, coast or overhang near.
    const auto [middleX, middleZ] = basin[basin.size() / 2];
    if (!pool.biomes.isEmpty() && !pool.biomes.contains(m_terrain.column(middleX, middleZ).biome)) {
        return std::nullopt;
    }
    const std::optional<GroundSpan> span = groundAround(basin);
    if (!span || span->highest - span->lowest > maxRise) {
        return std::nullopt;
    }
    // The liquid lies 1 or 2 blocks below the lowest ground of the rim.
    return span->lowest - (random < 0.5 ? 1 : 2);
}

std::optional<QcSurfaceLakes::GroundSpan> QcSurfaceLakes::groundAround(const QList<std::pair<int, int>> &basin) const
{
    GroundSpan span;
    const QSet<std::pair<int, int>> columns = withRing(basin);
    for (const auto &[x, z] : columns) {
        const std::optional<int> top = plainGroundAt(x, z);
        if (!top) {
            return std::nullopt;
        }
        span.lowest = std::min(span.lowest, *top);
        span.highest = std::max(span.highest, *top);
    }
    return span;
}

QSet<std::pair<int, int>> QcSurfaceLakes::withRing(const QList<std::pair<int, int>> &basin)
{
    QSet<std::pair<int, int>> columns;
    for (const auto &[bx, bz] : basin) {
        for (int z = bz - 1; z <= bz + 1; ++z) {
            for (int x = bx - 1; x <= bx + 1; ++x) {
                columns.insert({x, z});
            }
        }
    }
    return columns;
}

std::optional<int> QcSurfaceLakes::plainGroundAt(int x, int z) const
{
    const QcTerrainColumn column = m_terrain.column(x, z);
    const int top = column.height - 1;
    if (top <= m_seaLevel || column.landFactor < 0.999 || column.overhang >= 1.0 || column.riverBank
        || m_terrain.isRiverBiome(column.biome)) {
        return std::nullopt;
    }
    return top;
}

bool QcSurfaceLakes::isRock(QcBlockId block) const
{
    if (block == qcAirBlockId || !m_content->isValidBlock(block) || m_soil.contains(block)) {
        return false;
    }
    const QcBlockDefinition &definition = m_content->block(block);
    return definition.solid && !definition.liquid && !definition.replaceable;
}

void QcSurfaceLakes::carve(QcChunk &chunk, const Site &site, const QcTerrainColumnGrid &columns) const
{
    const Pool &pool = m_pools[site.pool];
    const QcBlockId rim = pool.rim != qcAirBlockId ? pool.rim : m_stone;
    const int baseX = chunk.pos().x * chunkSize;
    const int baseZ = chunk.pos().z * chunkSize;
    for (int z = 1; z < chunkSize - 1; ++z) {
        for (int x = 1; x < chunkSize - 1; ++x) {
            const int depth = depthAt(site, baseX + x, baseZ + z);
            if (depth < 1) {
                continue;
            }
            // Dug out above the level up to the plants on the ground, liquid below it.
            const int top = m_terrain.groundTop(baseX + x, baseZ + z, columns.at(baseX + x, baseZ + z));
            for (int y = site.level + 1; y <= std::min(QcWorld::worldHeight - 1, std::max(top, site.level) + 2); ++y) {
                chunk.setBlock(x, y, z, qcAirBlockId);
            }
            for (int y = site.level - depth + 1; y <= site.level; ++y) {
                chunk.setBlock(x, y, z, pool.liquid);
            }
        }
    }
    wallIn(chunk, site, rim);
    clearAround(chunk, site, columns, rim);
}

void QcSurfaceLakes::wallIn(QcChunk &chunk, const Site &site, QcBlockId rim) const
{
    // Every side and the bottom of the liquid that is not rock becomes the rim: no flow into air,
    // caves or soil, and a stone bed like the reference's.
    const QcBlockId liquid = m_pools[site.pool].liquid;
    const int baseX = chunk.pos().x * chunkSize;
    const int baseZ = chunk.pos().z * chunkSize;
    for (int z = 1; z < chunkSize - 1; ++z) {
        for (int x = 1; x < chunkSize - 1; ++x) {
            const int depth = depthAt(site, baseX + x, baseZ + z);
            for (int y = site.level - depth + 1; y <= site.level && depth >= 1; ++y) {
                sealAround(chunk, {x, y, z}, liquid, rim);
            }
        }
    }
}

void QcSurfaceLakes::sealAround(QcChunk &chunk, std::array<int, 3> at, QcBlockId liquid, QcBlockId rim) const
{
    // The four sides and the block below; the top stays open.
    static constexpr std::array<std::array<int, 3>, 5> sides{{{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}}};
    for (const auto &[dx, dy, dz] : sides) {
        const int x = at[0] + dx;
        const int y = at[1] + dy;
        const int z = at[2] + dz;
        const QcBlockId next = chunk.block(x, y, z);
        if (next != liquid && !isRock(next)) {
            chunk.setBlock(x, y, z, rim);
        }
    }
}

int QcSurfaceLakes::solidTopAt(const QcChunk &chunk, int x, int z, int from) const
{
    for (int y = std::min(from, QcWorld::worldHeight - 1); y > 0; --y) {
        const QcBlockId block = chunk.block(x, y, z);
        if (block != qcAirBlockId && m_content->isValidBlock(block) && m_content->block(block).solid) {
            return y;
        }
    }
    return 0;
}

void QcSurfaceLakes::clearAround(QcChunk &chunk, const Site &site, const QcTerrainColumnGrid &columns, QcBlockId rim) const
{
    // The first ring's ground turns to rock, and no plant grows within two blocks of the basin.
    const int baseX = chunk.pos().x * chunkSize;
    const int baseZ = chunk.pos().z * chunkSize;
    for (int z = 0; z < chunkSize; ++z) {
        for (int x = 0; x < chunkSize; ++x) {
            const int ring = ringAt(site, baseX + x, baseZ + z);
            if (ring == 0 || ring > clearRing) {
                continue;
            }
            const int top = solidTopAt(chunk, x, z, m_terrain.groundTop(baseX + x, baseZ + z, columns.at(baseX + x, baseZ + z)));
            if (ring == 1) {
                raiseRim(chunk, {x, top, z}, site.level, rim);
            }
            clearPlantsAbove(chunk, {x, top, z});
        }
    }
}

int QcSurfaceLakes::ringAt(const Site &site, int x, int z) const
{
    int nearest = clearRing + 1;
    for (int dz = -clearRing; dz <= clearRing; ++dz) {
        for (int dx = -clearRing; dx <= clearRing; ++dx) {
            if (depthAt(site, x + dx, z + dz) >= 1) {
                nearest = std::min(nearest, std::max(std::abs(dx), std::abs(dz)));
            }
        }
    }
    return nearest;
}

void QcSurfaceLakes::raiseRim(QcChunk &chunk, std::array<int, 3> ground, int level, QcBlockId rim) const
{
    const auto [x, top, z] = ground;
    // The rim stands above the liquid also where a cave opened the ground.
    for (int y = top + 1; y <= level + 1; ++y) {
        chunk.setBlock(x, y, z, rim);
    }
    if (m_soil.contains(chunk.block(x, top, z))) {
        chunk.setBlock(x, top, z, rim);
    }
}

void QcSurfaceLakes::clearPlantsAbove(QcChunk &chunk, std::array<int, 3> ground) const
{
    const auto [x, top, z] = ground;
    for (int y = top + 1; y <= std::min(QcWorld::worldHeight - 1, top + 2); ++y) {
        const QcBlockId block = chunk.block(x, y, z);
        if (block != qcAirBlockId && m_content->isValidBlock(block) && !m_content->block(block).solid
            && !m_content->block(block).liquid) {
            chunk.setBlock(x, y, z, qcAirBlockId);
        }
    }
}
