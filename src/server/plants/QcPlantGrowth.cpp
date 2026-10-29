// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/plants/QcPlantGrowth.h"

#include "common/blockrules/QcBlockRules.h"
#include "server/core/QcServerWorld.h"

#include <QList>

#include <algorithm>

QcPlantGrowth::QcPlantGrowth(std::shared_ptr<const QcContentRegistry> content, quint64 seed)
    : m_content(std::move(content))
    , m_random(static_cast<quint32>(seed ^ 0x6A0Fu))
    , m_crops(m_content, randomTickSeconds)
{
    for (QcBlockId id = 0; static_cast<qsizetype>(id) < m_content->stateCount(); ++id) {
        if (m_content->block(id).hasRandomTicks()) {
            m_growing.insert(id);
        }
    }
}

template<typename Visit>
void QcPlantGrowth::forRandomPlants(const QcChunk &chunk, quint32 sections, Visit visit)
{
    const QcChunkPos pos = chunk.pos();
    for (int index = 0; index < QcWorld::sectionCount; ++index) {
        if ((sections & (1u << index)) == 0 || !mayHoldPlants(chunk.section(index))) {
            continue;
        }

        for (int i = 0; i < m_randomTicksPerSection; ++i) {
            // A random cell of the section: x, z and y in 16 values each.
            const int cell = static_cast<int>(m_random.bounded(4096u));
            const int x = cell % 16;
            const int z = cell / 16 % 16;
            const int y = index * QcWorld::sectionHeight + cell / 256;
            if (m_growing.contains(chunk.block(x, y, z))) {
                visit(QcBlockPos{pos.x * QcWorld::chunkSize + x, y, pos.z * QcWorld::chunkSize + z});
            }
        }
    }
}

void QcPlantGrowth::tick(const QcServerWorld &world, const SetBlock &setBlock)
{
    // Only sections that may hold a growing plant: a scan of every loaded section per tick cost
    // more than the rest of the tick.
    const QcSectionMarks &marks = world.marks();
    if (m_growing.isEmpty() || marks.isEmpty(QcBlockTrait::RandomTicks)) {
        return;
    }

    // In a fixed order of chunks, so the same seed draws the same random cells in every run (the
    // hash order differs between processes).
    const QList<QcChunkPos> &order = marks.orderedChunks(QcBlockTrait::RandomTicks);

    // Collected first: setting blocks while holding a chunk snapshot would copy the chunk.
    QList<QcBlockPos> ticked;
    for (const QcChunkPos pos : std::as_const(order)) {
        if (const std::shared_ptr<const QcChunk> chunk = world.chunk(pos)) {
            forRandomPlants(*chunk, marks.sectionsOf(pos, QcBlockTrait::RandomTicks), [&ticked](QcBlockPos plant) { ticked.append(plant); });
        }
    }

    for (const QcBlockPos &pos : std::as_const(ticked)) {
        randomTick(world, pos, setBlock);
    }
}

bool QcPlantGrowth::randomTick(const QcServerWorld &world, QcBlockPos pos, const SetBlock &setBlock)
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    if (!block || !m_growing.contains(*block)) {
        return false;
    }
    if (!m_content->block(*block).growth.grows()) {
        return m_crops.randomTick(world, pos, *block, m_random, setBlock);
    }
    if (m_random.generateDouble() >= growthChance(*block)) {
        return false;
    }

    const std::optional<QcBlockPos> target = growthTarget(world, pos);
    const std::optional<QcBlockId> grown = m_content->blockId(m_content->block(*block).name);
    return target && grown && setBlock(*target, *grown);
}

std::optional<QcBlockPos> QcPlantGrowth::growthTarget(const QcBlockAccess &world, QcBlockPos pos) const
{
    const std::optional<QcBlockId> block = world.blockAt(pos);
    if (!block || !m_growing.contains(*block) || !m_content->block(*block).growth.grows()) {
        return std::nullopt;
    }

    // Only the top block grows; the column counts every block of the same kind below it.
    const QcBlockPos above = pos.offset(0, 1, 0);
    if (world.blockAt(above) != qcAirBlockId || !above.isInsideWorldHeight()) {
        return std::nullopt;
    }
    const auto samePlant = [&](QcBlockPos at) {
        const std::optional<QcBlockId> other = world.blockAt(at);
        return other && m_content->isValidBlock(*other) && m_content->isSameBlock(*other, *block);
    };
    QcBlockPos base = pos;
    int height = 1;
    while (samePlant(base.offset(0, -1, 0))) {
        base = base.offset(0, -1, 0);
        ++height;
    }

    const bool rooted = QcBlockRules::isSupported(world, base, *block) && world.blockAt(base.offset(0, -1, 0)).has_value();
    if (height >= m_content->block(*block).growth.maxHeight || !rooted) {
        return std::nullopt;
    }
    return above;
}

double QcPlantGrowth::growthChance(QcBlockId block) const
{
    const QcGrowthDefinition &growth = m_content->block(block).growth;
    return growth.grows() ? std::min(1.0, randomTickSeconds / growth.averageSeconds) : 0.0;
}

bool QcPlantGrowth::mayHoldPlants(const QcChunkSection &section) const
{
    if (section.isEmpty()) {
        return false;
    }
    const QList<QcBlockId> &palette = section.palette();
    return std::ranges::any_of(palette, [this](QcBlockId id) { return m_growing.contains(id); });
}
