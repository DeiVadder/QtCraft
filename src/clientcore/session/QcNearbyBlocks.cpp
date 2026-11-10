// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcNearbyBlocks.h"

#include <algorithm>
#include <cmath>

QcNearbyBlocks::QcNearbyBlocks(Reach reach, Sort sort)
    : m_reach(reach)
    , m_sort(std::move(sort))
{
}

void QcNearbyBlocks::refresh(const QcClientWorld &world, const QcVec3 &eye, quint64 tick)
{
    if (m_content != &world.content()) {
        learn(world.content());
        m_scanned = false;
    }
    if (m_scanned && tick < m_scannedAt + static_cast<quint64>(m_reach.rescanTicks) && tick >= m_scannedAt) {
        return;
    }
    m_found = scan(world, eye);
    m_scannedAt = tick;
    m_scanned = true;
}

void QcNearbyBlocks::learn(const QcContentRegistry &content)
{
    m_content = &content;
    m_picks.assign(static_cast<std::size_t>(content.stateCount()), false);
    for (std::size_t id = 0; id < m_picks.size(); ++id) {
        m_picks[id] = m_sort(content, static_cast<QcBlockId>(id));
    }
}

bool QcNearbyBlocks::picks(QcBlockId id) const
{
    return static_cast<std::size_t>(id) < m_picks.size() && m_picks[static_cast<std::size_t>(id)];
}

QList<QcBlockPos> QcNearbyBlocks::inSection(const QcChunkSection &section, QcBlockPos origin) const
{
    QList<QcBlockPos> found;
    if (!std::ranges::any_of(section.palette(), [this](QcBlockId id) { return picks(id); })) {
        return found;
    }

    for (int cell = 0; cell < QcWorld::chunkSize * QcWorld::chunkSize * QcWorld::sectionHeight; ++cell) {
        const int x = cell % QcWorld::chunkSize;
        const int z = cell / QcWorld::chunkSize % QcWorld::chunkSize;
        const int y = cell / (QcWorld::chunkSize * QcWorld::chunkSize);
        if (picks(section.block(x, y, z))) {
            found.append({origin.x + x, origin.y + y, origin.z + z});
        }
    }
    return found;
}

QList<QcBlockPos> QcNearbyBlocks::scan(const QcClientWorld &world, const QcVec3 &eye) const
{
    const int radius = m_reach.radius;
    const QcBlockPos centre{static_cast<int>(std::floor(eye.x)), static_cast<int>(std::floor(eye.y)), static_cast<int>(std::floor(eye.z))};
    const int lowest = std::max(0, (centre.y - radius) / QcWorld::sectionHeight);
    const int highest = std::min(QcWorld::sectionCount - 1, (centre.y + radius) / QcWorld::sectionHeight);
    QList<QcBlockPos> found;
    for (int cx = (centre.x - radius) >> 4; cx <= (centre.x + radius) >> 4; ++cx) {
        for (int cz = (centre.z - radius) >> 4; cz <= (centre.z + radius) >> 4; ++cz) {
            const std::shared_ptr<const QcChunk> chunk = world.chunk({cx, cz});
            for (int index = lowest; chunk && index <= highest; ++index) {
                found.append(inSection(chunk->section(index), {cx * QcWorld::chunkSize, index * QcWorld::sectionHeight, cz * QcWorld::chunkSize}));
            }
        }
    }

    // The nearest first, at most `most`.
    const auto distance = [&eye](QcBlockPos pos) { return (QcVec3{pos.x + 0.5, pos.y + 0.5, pos.z + 0.5} - eye).length(); };
    std::ranges::sort(found, {}, distance);
    found.resize(std::min<qsizetype>(found.size(), m_reach.most));
    return found;
}
