#pragma once

#include "worldgen/terrain/QcTerrainGenerator.h"
#include "worldgen/villages/QcVillageGenerator.h"

#include <QHash>
#include <QList>

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

// Helpers of the village generation tests (decision E-403, E-404): plans of a seed, the chunks of a
// plan generated in order or backwards on several threads, blocks of generated chunks.
namespace QcVillageTestUtil {

using Chunks = QHash<QcChunkPos, std::shared_ptr<QcChunk>>;

// The villages of the regions within `rings` of the origin.
inline QList<std::shared_ptr<const QcVillagePlan>> plansAround(const QcTerrainGenerator &generator, int rings)
{
    QList<std::shared_ptr<const QcVillagePlan>> plans;
    for (int rz = -rings; rz <= rings; ++rz) {
        for (int rx = -rings; rx <= rings; ++rx) {
            if (std::shared_ptr<const QcVillagePlan> plan = generator.villages().regionPlan(rx, rz)) {
                plans.append(std::move(plan));
            }
        }
    }
    return plans;
}

// A fixed sample for the statistical tests (decision E-424): the first `count` villages found ring
// by ring of regions around the origin (at most `maxRings` rings); fewer only when the rings run out,
// which the tests check.
inline QList<std::shared_ptr<const QcVillagePlan>> samplePlans(const QcTerrainGenerator &generator, qsizetype count, int maxRings)
{
    QList<std::shared_ptr<const QcVillagePlan>> plans;
    for (int ring = 0; ring <= maxRings && plans.size() < count; ++ring) {
        for (int rz = -ring; rz <= ring && plans.size() < count; ++rz) {
            for (int rx = -ring; rx <= ring && plans.size() < count; ++rx) {
                if (std::max(std::abs(rx), std::abs(rz)) != ring) {
                    continue;
                }
                if (std::shared_ptr<const QcVillagePlan> plan = generator.villages().regionPlan(rx, rz)) {
                    plans.append(std::move(plan));
                }
            }
        }
    }
    return plans;
}

inline QList<QcChunkPos> chunksOf(const QcVillagePlan &plan)
{
    QList<QcChunkPos> chunks;
    const QcChunkPos low = QcBlockPos{plan.minX, 0, plan.minZ}.chunk();
    const QcChunkPos high = QcBlockPos{plan.maxX, 0, plan.maxZ}.chunk();
    for (int cz = low.z; cz <= high.z; ++cz) {
        for (int cx = low.x; cx <= high.x; ++cx) {
            chunks.append({cx, cz});
        }
    }
    return chunks;
}

inline Chunks generateInOrder(const QcTerrainGenerator &generator, const QList<QcChunkPos> &positions)
{
    Chunks chunks;
    for (const QcChunkPos &pos : positions) {
        chunks.insert(pos, generator.generate(pos));
    }
    return chunks;
}

// The chunks generated backwards, spread over four threads, by `generator`.
inline Chunks generateOnThreads(const QcTerrainGenerator &generator, const QList<QcChunkPos> &chunks)
{
    std::vector<std::shared_ptr<QcChunk>> results(static_cast<std::size_t>(chunks.size()));
    constexpr int threadCount = 4;
    std::vector<std::thread> threads;
    threads.reserve(threadCount);
    for (int t = 0; t < threadCount; ++t) {
        threads.emplace_back([&generator, &chunks, &results, t] {
            for (qsizetype i = chunks.size() - 1 - t; i >= 0; i -= threadCount) {
                results[static_cast<std::size_t>(i)] = generator.generate(chunks[i]);
            }
        });
    }
    for (std::thread &thread : threads) {
        thread.join();
    }

    Chunks generated;
    for (qsizetype i = 0; i < chunks.size(); ++i) {
        generated.insert(chunks[i], results[static_cast<std::size_t>(i)]);
    }
    return generated;
}

inline std::optional<QcBlockId> blockIn(const Chunks &chunks, QcBlockPos pos)
{
    const auto it = chunks.constFind(pos.chunk());
    if (it == chunks.cend() || !pos.isInsideWorldHeight()) {
        return std::nullopt;
    }
    return (*it)->block(pos.localX(), pos.y, pos.localZ());
}

inline bool sameBlocks(const QcChunk &a, const QcChunk &b)
{
    for (int cell = 0; cell < QcWorld::chunkSize * QcWorld::chunkSize * QcWorld::worldHeight; ++cell) {
        const int x = cell % QcWorld::chunkSize;
        const int z = cell / QcWorld::chunkSize % QcWorld::chunkSize;
        const int y = cell / (QcWorld::chunkSize * QcWorld::chunkSize);
        if (a.block(x, y, z) != b.block(x, y, z)) {
            return false;
        }
    }
    return true;
}

// Every placed cell of the piece holds its state.
inline bool pieceIsBuilt(const Chunks &chunks, const QcStructurePlacement &piece)
{
    const QcStructure &structure = piece.structure();
    const int layer = structure.sizeX() * structure.sizeZ();
    for (int cell = 0; cell < layer * structure.sizeY(); ++cell) {
        const QcBlockPos local{cell % structure.sizeX(), cell / layer, cell % layer / structure.sizeX()};
        const std::optional<QcBlockId> state = piece.worldBlock(local);
        if (state && blockIn(chunks, piece.toWorld(local)) != state) {
            return false;
        }
    }
    return true;
}

// The floor rests on solid ground: no gap below any column of the piece.
inline bool standsOnGround(const QcContentRegistry &content, const Chunks &chunks, const QcStructurePlacement &piece)
{
    const QcBlockPos origin = piece.origin();
    for (int cell = 0; cell < piece.sizeX() * piece.sizeZ(); ++cell) {
        const std::optional<QcBlockId> below = blockIn(chunks, {origin.x + cell % piece.sizeX(), origin.y - 1, origin.z + cell / piece.sizeX()});
        if (!below || !content.block(*below).solid) {
            return false;
        }
    }
    return true;
}

// The short name of a piece's structure ("village_plains_pen").
inline QString shortName(const QcStructurePlacement &piece)
{
    const QString &name = piece.structure().name();
    return name.mid(name.indexOf(u':') + 1);
}

} // namespace QcVillageTestUtil
