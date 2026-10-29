// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/light/QcLightProbe.h"

#include "clientcore/session/QcClientWorld.h"
#include "common/core/QcWorldConstants.h"
#include "common/world/QcLightPassage.h"

#include <algorithm>
#include <cstdlib>

QcLightProbe::QcLightProbe(const QcClientWorld &world, QcBlockPos centre, int radius)
    : m_centre(centre)
    , m_radius(std::clamp(radius, 0, maxRadius))
    , m_reach(m_radius + QcLightVolume::maxLevel)
    , m_size(2 * m_reach + 1)
    , m_low(centre.offset(-m_reach, -m_reach, -m_reach))
{
    if (m_radius > 0 || !centreUnderOpenSky(world)) {
        m_sky.emplace(m_size, m_size, m_size);
    }
    if (world.mayHaveLightSourceNear(centre, m_reach)) {
        m_block.emplace(m_size, m_size, m_size);
    }
    if (!m_sky && !m_block) {
        return;
    }

    fill(world);
    if (m_sky) {
        m_sky->propagate();
    }
    if (m_block) {
        m_block->propagate();
    }
}

bool QcLightProbe::centreUnderOpenSky(const QcClientWorld &world) const
{
    if (m_centre.y >= QcWorld::worldHeight) {
        return true;
    }
    const std::shared_ptr<const QcChunk> chunk = world.chunk(m_centre.chunk());
    return chunk && m_centre.y >= 0 && m_centre.y > columnTop(chunk.get(), m_centre.localX(), m_centre.localZ(), world.content());
}

QcLightProbe::Levels QcLightProbe::at(QcBlockPos pos) const
{
    const bool inside = std::abs(pos.x - m_centre.x) <= m_radius && std::abs(pos.y - m_centre.y) <= m_radius
                        && std::abs(pos.z - m_centre.z) <= m_radius;
    if (!inside || pos.y < 0) {
        return {};
    }

    const int x = pos.x - m_low.x;
    const int y = pos.y - m_low.y;
    const int z = pos.z - m_low.z;
    return {m_sky ? m_sky->level(x, y, z) : QcLightVolume::maxLevel, m_block ? m_block->level(x, y, z) : 0};
}

void QcLightProbe::fill(const QcClientWorld &world)
{
    const QcContentRegistry &content = world.content();
    QHash<QcChunkPos, std::shared_ptr<const QcChunk>> chunks;
    for (int z = 0; z < m_size; ++z) {
        for (int x = 0; x < m_size; ++x) {
            const QcChunkPos chunkPos = m_low.offset(x, 0, z).chunk();
            if (!chunks.contains(chunkPos)) {
                chunks.insert(chunkPos, world.chunk(chunkPos));
            }
            fillColumn(chunks.value(chunkPos).get(), x, z, content);
        }
    }
}

void QcLightProbe::fillColumn(const QcChunk *chunk, int x, int z, const QcContentRegistry &content)
{
    const QcBlockPos column = m_low.offset(x, 0, z);
    const int top = columnTop(chunk, column.localX(), column.localZ(), content);

    for (int y = 0; y < m_size; ++y) {
        const int worldY = m_low.y + y;
        if (worldY >= QcWorld::worldHeight) {
            if (m_sky) {
                m_sky->addSource(x, y, z, QcLightVolume::maxLevel);
            }
            continue;
        }

        const Cell cell = cellAt(chunk, {column.x, worldY, column.z}, content);
        if (m_sky) {
            m_sky->setOpacity(x, y, z, cell.opacity, cell.blockedFaces);
            m_sky->addSource(x, y, z, chunk && worldY > top ? QcLightVolume::maxLevel : 0);
        }
        if (m_block) {
            m_block->setOpacity(x, y, z, cell.opacity, cell.blockedFaces);
            m_block->addSource(x, y, z, cell.light);
        }
    }
}

QcLightProbe::Cell QcLightProbe::cellAt(const QcChunk *chunk, QcBlockPos pos, const QcContentRegistry &content)
{
    // Unknown cells and the void below the world are dark walls.
    if (!chunk || pos.y < 0) {
        return {QcLightVolume::maxLevel, 0, 0};
    }

    const QcBlockId id = chunk->block(pos.localX(), pos.y, pos.localZ());
    if (!content.isValidBlock(id)) {
        return {};
    }
    const QcBlockState &state = content.state(id);
    return {state.lightOpacity, state.lightBlockedFaces, content.block(id).lightLevel};
}

int QcLightProbe::columnTop(const QcChunk *chunk, int lx, int lz, const QcContentRegistry &content)
{
    if (!chunk) {
        return QcWorld::worldHeight;
    }

    for (int section = QcWorld::sectionCount - 1; section >= 0; --section) {
        if (chunk->section(section).isEmpty()) {
            continue;
        }
        for (int y = (section + 1) * QcWorld::sectionHeight - 1; y >= section * QcWorld::sectionHeight; --y) {
            const QcBlockId id = chunk->block(lx, y, lz);
            if (!content.isValidBlock(id)) {
                continue;
            }
            const QcBlockState &state = content.state(id);
            const QcLightPassage::SkyStop stop = QcLightPassage::skyStop(state.lightOpacity, state.lightBlockedFaces);
            if (stop != QcLightPassage::SkyStop::None) {
                return QcLightPassage::skyTop(stop, y);
            }
        }
    }
    return -1;
}
