// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/meshing/QcMeshLighting.h"

#include "common/world/QcLightPassage.h"

#include <algorithm>
#include <utility>

namespace {

// The levels of the section starting at `baseY`, read cell by cell (chunk-local coordinates).
template<typename Level>
std::shared_ptr<const QcLightNibbles> copyCells(int baseY, Level level)
{
    auto cells = std::make_shared<QcLightNibbles>();
    for (int y = 0; y < QcWorld::sectionHeight; ++y) {
        for (int z = 0; z < QcWorld::chunkSize; ++z) {
            for (int x = 0; x < QcWorld::chunkSize; ++x) {
                cells->setLevel(QcChunkSection::index(x, y, z), static_cast<int>(level(x, baseY + y, z)));
            }
        }
    }
    return cells;
}

} // namespace

QcMeshLighting::QcMeshLighting(QcMeshNeighbourhood around, const QcMeshInput &input)
    : m_minY(std::max(0, input.sectionIndex * QcWorld::sectionHeight - margin))
    , m_maxY(std::min(QcWorld::worldHeight - 1, (input.sectionIndex + 1) * QcWorld::sectionHeight - 1 + margin))
    , m_height(m_maxY - m_minY + 1)
    , m_levels(static_cast<qsizetype>(extent) * extent * m_height, 0)
    , m_opacity(static_cast<qsizetype>(extent) * extent * m_height, 0)
    , m_blockedFaces(static_cast<qsizetype>(extent) * extent * m_height, 0)
    , m_floodsBlockLight(input.renderTable->hasLightSources() && !input.hasBlockLight)
    , m_blockLight(m_floodsBlockLight ? extent : 0, m_height, extent)
    , m_input(input)
{
    QList<int> skyTop(static_cast<qsizetype>(extent) * extent, 0);
    for (int z = 0; z < extent; ++z) {
        for (int x = 0; x < extent; ++x) {
            const int top = columnTop(around, x - QcWorld::chunkSize, z - QcWorld::chunkSize);
            skyTop[z * extent + x] = top;
            fillColumn(around, x, z, top);
        }
    }

    propagate(skyTop);
    m_blockLight.propagate();
}

QcSectionLight QcMeshLighting::sectionLight(int sectionIndex) const
{
    const int baseY = sectionIndex * QcWorld::sectionHeight;
    std::shared_ptr<const QcLightNibbles> blockCells;
    if (m_input.hasBlockLight) {
        const auto &chunk = m_input.blockLight[4];
        blockCells = chunk ? chunk->sections[static_cast<std::size_t>(sectionIndex)] : nullptr;
    } else {
        blockCells = copyCells(baseY, [this](int x, int y, int z) { return block(x, y, z); });
    }

    const auto skyCells = copyCells(baseY, [this](int x, int y, int z) { return m_levels[index(x + QcWorld::chunkSize, y, z + QcWorld::chunkSize)]; });
    return QcSectionLight::fromChannels(std::move(blockCells), skyCells);
}

// Highest cell of the column that sky light coming straight down does not reach; everything
// above it is fully lit (QcLightPassage::skyStop).
int QcMeshLighting::columnTop(QcMeshNeighbourhood around, int lx, int lz) const
{
    const int chunkIndex = (lz < 0 ? 0 : (lz >= QcWorld::chunkSize ? 2 : 1)) * 3
                           + (lx < 0 ? 0 : (lx >= QcWorld::chunkSize ? 2 : 1));
    const auto &chunk = m_input.chunks[static_cast<std::size_t>(chunkIndex)];
    if (!chunk) {
        return QcWorld::worldHeight;
    }

    const QcBlockRenderTable &table = *m_input.renderTable;
    for (int section = QcWorld::sectionCount - 1; section >= 0; --section) {
        if (chunk->section(section).isEmpty()) {
            continue;
        }
        for (int y = (section + 1) * QcWorld::sectionHeight - 1; y >= section * QcWorld::sectionHeight; --y) {
            const std::optional<QcBlockId> id = around.block(lx, y, lz);
            const QcBlockRenderTable::Entry &entry = table.entry(id.value_or(qcAirBlockId));
            const QcLightPassage::SkyStop stop = QcLightPassage::skyStop(entry.lightOpacity, entry.lightBlockedFaces);
            if (id && stop != QcLightPassage::SkyStop::None) {
                return QcLightPassage::skyTop(stop, y);
            }
        }
    }
    return -1;
}

// Opacity, full sky light above `top` and block light sources of one column of the volume.
void QcMeshLighting::fillColumn(QcMeshNeighbourhood around, int x, int z, int top)
{
    const QcBlockRenderTable &table = *m_input.renderTable;
    const int lx = x - QcWorld::chunkSize;
    const int lz = z - QcWorld::chunkSize;
    for (int y = m_minY; y <= m_maxY; ++y) {
        const std::optional<QcBlockId> id = around.block(lx, y, lz);
        const QcBlockRenderTable::Entry &entry = table.entry(id.value_or(qcAirBlockId));
        const int opacity = id ? entry.lightOpacity : maxLevel;
        m_opacity[index(x, y, z)] = static_cast<quint8>(opacity);
        m_blockedFaces[index(x, y, z)] = entry.lightBlockedFaces;
        if (y > top) {
            m_levels[index(x, y, z)] = static_cast<quint8>(maxLevel);
        }

        if (!m_floodsBlockLight) {
            continue;
        }
        m_blockLight.setOpacity(x, y - m_minY, z, opacity, entry.lightBlockedFaces);
        if (id && entry.lightLevel > 0) {
            m_blockLight.addSource(x, y - m_minY, z, entry.lightLevel);
        }
    }
}

// Only the lit cells next to the shadowed part of the volume can spread light further; queues
// those of column (x, z) for the flood.
void QcMeshLighting::queueSkyEdge(const QList<int> &skyTop, int x, int z)
{
    const int top = skyTop[z * extent + x];
    for (int y = std::max(m_minY, top + 1); y <= std::min(m_maxY, top + 2); ++y) {
        m_queue.append({x, y, z});
    }

    for (const auto &[dx, dz] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
        const int nx = x + dx;
        const int nz = z + dz;
        if (nx < 0 || nx >= extent || nz < 0 || nz >= extent) {
            continue;
        }
        const int neighbourTop = skyTop[nz * extent + nx];
        for (int y = std::max(m_minY, top + 3); y <= std::min(m_maxY, neighbourTop); ++y) {
            m_queue.append({x, y, z});
        }
    }
}

void QcMeshLighting::propagate(const QList<int> &skyTop)
{
    // Breadth-first flood from lit cells into darker ones; each step costs at least one level.
    for (int z = 0; z < extent; ++z) {
        for (int x = 0; x < extent; ++x) {
            queueSkyEdge(skyTop, x, z);
        }
    }

    QList<std::array<int, 3>> &queue = m_queue;

    for (qsizetype head = 0; head < queue.size(); ++head) {
        const auto [x, y, z] = queue[head];
        const int light = m_levels[index(x, y, z)];
        if (light <= 1) {
            continue;
        }
        const quint8 blocked = m_blockedFaces[index(x, y, z)];
        for (const QcBlockFace face : qcAllBlockFaces) {
            const QcFaceOffset offset = qcFaceOffset(face);
            const int nx = x + offset.dx;
            const int ny = y + offset.dy;
            const int nz = z + offset.dz;
            if (nx < 0 || nx >= extent || nz < 0 || nz >= extent || ny < m_minY || ny > m_maxY) {
                continue;
            }
            const qsizetype neighbour = index(nx, ny, nz);
            if (!QcLightPassage::crosses(blocked, m_blockedFaces[neighbour], face)) {
                continue;
            }
            const int opacity = m_opacity[neighbour];
            const int candidate = light - std::max(1, opacity);
            const int current = m_levels[neighbour];
            if (opacity < maxLevel && candidate > current) {
                m_levels[neighbour] = static_cast<quint8>(candidate);
                queue.append({nx, ny, nz});
            }
        }
    }
    // The queue is only needed while flooding.
    m_queue = {};
}
