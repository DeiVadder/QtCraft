// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/world/QcLightVolume.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcBlockFace.h"
#include "common/world/QcLightPassage.h"

#include <algorithm>
#include <utility>

namespace {

// Whether a section from `first` to `last` of `chunk` holds a block that emits light.
bool sectionsEmitLight(const QcContentRegistry &content, const QcChunk &chunk, int first, int last)
{
    const auto emits = [&content](QcBlockId id) { return content.isValidBlock(id) && content.block(id).lightLevel > 0; };
    for (int section = first; section <= last; ++section) {
        if (std::ranges::any_of(chunk.section(section).palette(), emits)) {
            return true;
        }
    }
    return false;
}

} // namespace

QcLightVolume::QcLightVolume(int sizeX, int sizeY, int sizeZ)
    : m_sizeX(std::max(0, sizeX))
    , m_sizeY(std::max(0, sizeY))
    , m_sizeZ(std::max(0, sizeZ))
    , m_levels(static_cast<qsizetype>(m_sizeX) * m_sizeY * m_sizeZ, 0)
    , m_opacity(static_cast<qsizetype>(m_sizeX) * m_sizeY * m_sizeZ, 0)
    , m_blockedFaces(static_cast<qsizetype>(m_sizeX) * m_sizeY * m_sizeZ, 0)
{
}

void QcLightVolume::setOpacity(int x, int y, int z, int opacity, quint8 blockedFaces)
{
    if (contains(x, y, z)) {
        m_opacity[index(x, y, z)] = static_cast<quint8>(std::clamp(opacity, 0, maxLevel));
        m_blockedFaces[index(x, y, z)] = blockedFaces;
    }
}

void QcLightVolume::addSource(int x, int y, int z, int level)
{
    if (!contains(x, y, z) || level <= 0) {
        return;
    }

    quint8 &current = m_levels[index(x, y, z)];
    if (std::cmp_greater(level, current)) {
        current = static_cast<quint8>(std::min(level, maxLevel));
        m_queue.append({x, y, z});
    }
}

void QcLightVolume::propagate()
{
    for (qsizetype head = 0; head < m_queue.size(); ++head) {
        const auto [x, y, z] = m_queue[head];
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
            if (!contains(nx, ny, nz) || !QcLightPassage::crosses(blocked, m_blockedFaces[index(nx, ny, nz)], face)) {
                continue;
            }

            const qsizetype neighbour = index(nx, ny, nz);
            const int opacity = m_opacity[neighbour];
            const int candidate = light - std::max(1, opacity);
            if (opacity < maxLevel && std::cmp_greater(candidate, m_levels[neighbour])) {
                m_levels[neighbour] = static_cast<quint8>(candidate);
                m_queue.append({nx, ny, nz});
            }
        }
    }
    m_queue.clear();
}

int QcLightVolume::blockLightAt(const QcBlockAccess &world, QcBlockPos pos)
{
    // Light travels at most 14 blocks from a source of level 15.
    constexpr int reach = maxLevel;
    constexpr int size = 2 * reach + 1;
    if (!world.mayHaveLightSourceNear(pos, reach)) {
        return 0;
    }

    QcLightVolume volume(size, size, size);
    const QcBlockPos low = pos.offset(-reach, -reach, -reach);
    for (int y = 0; y < size; ++y) {
        for (int z = 0; z < size; ++z) {
            for (int x = 0; x < size; ++x) {
                volume.setCell(world, x, y, z, low.offset(x, y, z));
            }
        }
    }

    volume.propagate();
    return volume.level(reach, reach, reach);
}

void QcLightVolume::setCell(const QcBlockAccess &world, int x, int y, int z, QcBlockPos cell)
{
    // Unknown cells are dark walls; addSource ignores level 0.
    const QcContentRegistry &content = world.content();
    const std::optional<QcBlockId> block = cell.y < 0 ? std::nullopt : world.blockAt(cell);
    if (!block || !content.isValidBlock(*block)) {
        setOpacity(x, y, z, maxLevel);
        return;
    }

    const QcBlockState &state = content.state(*block);
    setOpacity(x, y, z, state.lightOpacity, state.lightBlockedFaces);
    addSource(x, y, z, content.block(*block).lightLevel);
}

bool QcLightVolume::anySourceNear(const QcContentRegistry &content, QcBlockPos pos, int reach,
                                  const std::function<const QcChunk *(QcChunkPos)> &chunkAt)
{
    const QcBlockPos low = pos.offset(-reach, -reach, -reach);
    const QcBlockPos high = pos.offset(reach, reach, reach);
    const int first = std::max(0, low.y / QcWorld::sectionHeight);
    const int last = std::min(QcWorld::sectionCount - 1, high.y / QcWorld::sectionHeight);

    for (int cx = low.chunk().x; cx <= high.chunk().x; ++cx) {
        for (int cz = low.chunk().z; cz <= high.chunk().z; ++cz) {
            const QcChunk *chunk = chunkAt({cx, cz});
            if (chunk && sectionsEmitLight(content, *chunk, first, last)) {
                return true;
            }
        }
    }
    return false;
}
