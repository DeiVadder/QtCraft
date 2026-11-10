// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/nether/QcNetherSpawn.h"

#include "common/blockrules/QcSafeStand.h"
#include "common/world/QcBlockAccess.h"
#include "common/world/QcChunk.h"
#include "worldgen/nether/QcNetherGenerator.h"

#include <QList>

#include <array>
#include <memory>
#include <utility>

namespace {

// Free cells over a floor of the bare rock.
constexpr int rockRoom = 3;
// The columns of a chunk the search looks at: its middle and four around it, two blocks or more
// from its edges so the cells around them lie in the chunk.
constexpr std::array<std::pair<int, int>, 5> columns{{{8, 8}, {4, 4}, {12, 4}, {4, 12}, {12, 12}}};

// The cells of the square ring of `ring` around the origin, each once.
QList<std::pair<int, int>> ringCells(int ring)
{
    if (ring == 0) {
        return {{0, 0}};
    }
    QList<std::pair<int, int>> cells;
    cells.reserve(qsizetype{ring} * 8);
    for (int along = -ring; along < ring; ++along) {
        cells.append({along, -ring});
        cells.append({ring, along});
        cells.append({-along, ring});
        cells.append({-ring, -along});
    }
    return cells;
}

// The blocks of one generated chunk; every other cell reads as not loaded.
class ChunkBlocks final : public QcBlockAccess
{
public:
    ChunkBlocks(std::shared_ptr<const QcChunk> chunk, const QcContentRegistry &content)
        : m_chunk(std::move(chunk))
        , m_content(content)
    {
    }

    [[nodiscard]] std::optional<QcBlockId> blockAt(QcBlockPos pos) const override
    {
        if (pos.chunk() != m_chunk->pos() || pos.y < 0) {
            return std::nullopt;
        }
        if (pos.y >= QcWorld::worldHeight) {
            return qcAirBlockId;
        }
        return m_chunk->block(pos.localX(), pos.y, pos.localZ());
    }
    const QcContentRegistry &content() const override { return m_content; }

private:
    std::shared_ptr<const QcChunk> m_chunk;
    const QcContentRegistry &m_content;
};

} // namespace

QcNetherSpawn::QcNetherSpawn(const QcNetherGenerator &generator, const QcContentRegistry &content)
    : m_generator(generator)
    , m_content(content)
{
}

QcBlockPos QcNetherSpawn::find() const
{
    if (!m_generator.definitions().settings.spawnBiomes.isEmpty()) {
        if (const std::optional<QcBlockPos> spawn = onRings(preferredRings, true)) {
            return *spawn;
        }
    }
    if (const std::optional<QcBlockPos> spawn = onRings(rings, false)) {
        return *spawn;
    }
    return onRock();
}

std::optional<QcBlockPos> QcNetherSpawn::onRings(int count, bool preferred) const
{
    for (int ring = 0; ring <= count; ++ring) {
        const QList<std::pair<int, int>> cells = ringCells(ring);
        for (const auto &[chunkX, chunkZ] : cells) {
            if (const std::optional<QcBlockPos> spawn = inChunk(chunkX, chunkZ, preferred)) {
                return spawn;
            }
        }
    }
    return std::nullopt;
}

std::optional<QcBlockPos> QcNetherSpawn::inChunk(int chunkX, int chunkZ, bool preferred) const
{
    const QcChunkPos pos{chunkX, chunkZ};
    const int baseX = chunkX * QcWorld::chunkSize;
    const int baseZ = chunkZ * QcWorld::chunkSize;
    // Cheap tests first: the biome of its middle and the bare rock, before the chunk is generated.
    if ((preferred && !preferredAt(baseX + 8, baseZ + 8)) || m_generator.hasResidentsIn(pos)) {
        return std::nullopt;
    }
    const QcNetherSettings &settings = m_generator.definitions().settings;
    if (!m_generator.terrain().floorAt(baseX + 8, baseZ + 8, 0, rockRoom)) {
        return std::nullopt;
    }

    const ChunkBlocks blocks(m_generator.generate(pos), m_content);
    for (const auto &[localX, localZ] : columns) {
        for (int y = settings.liquidLevel + 1; y + 2 < settings.roof; ++y) {
            const QcBlockPos feet{baseX + localX, y, baseZ + localZ};
            if (QcSafeStand::fits(blocks, feet)) {
                return feet;
            }
        }
    }
    return std::nullopt;
}

bool QcNetherSpawn::preferredAt(int x, int z) const
{
    return m_generator.definitions().settings.spawnBiomes.contains(m_generator.biomeAt(x, z));
}

QcBlockPos QcNetherSpawn::onRock() const
{
    for (int ring = 0; ring <= rings; ++ring) {
        const QList<std::pair<int, int>> cells = ringCells(ring);
        for (const auto &[chunkX, chunkZ] : cells) {
            const int x = chunkX * QcWorld::chunkSize;
            const int z = chunkZ * QcWorld::chunkSize;
            if (const std::optional<int> floor = m_generator.terrain().floorAt(x, z, 0, rockRoom)) {
                return {x, *floor + 1, z};
            }
        }
    }
    return {0, m_generator.definitions().settings.liquidLevel + 2, 0};
}
