// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcXzRect.h"
#include "common/world/QcChunk.h"
#include "worldgen/generators/QcWorldGenerator.h"

#include <QHash>
#include <QSet>

#include <deque>
#include <functional>
#include <memory>

// The chunks over a rectangle and one chunk beyond it, generated once, to be read by world cell:
// chunks are dear in Debug, so the tests of a structure share these.
class QcChunkSite
{
public:
    QcChunkSite(const QcWorldGenerator &generator, QcXzRect bounds)
    {
        const QcBlockPos low{bounds.minX, 0, bounds.minZ};
        const QcBlockPos high{bounds.maxX, 0, bounds.maxZ};
        for (int z = low.chunk().z - 1; z <= high.chunk().z + 1; ++z) {
            for (int x = low.chunk().x - 1; x <= high.chunk().x + 1; ++x) {
                const QcChunkPos pos{x, z};
                m_chunks.insert(key(pos), generator.generate(pos));
            }
        }
    }

    [[nodiscard]] QcBlockId at(QcBlockPos cell) const
    {
        const QcChunkPos pos = cell.chunk();
        const std::shared_ptr<QcChunk> &chunk = m_chunks[key(pos)];
        return chunk->block(cell.x - pos.x * QcWorld::chunkSize, cell.y, cell.z - pos.z * QcWorld::chunkSize);
    }

private:
    static qint64 key(QcChunkPos pos) { return (static_cast<qint64>(pos.x) << 32) ^ static_cast<qint64>(static_cast<quint32>(pos.z)); }

    mutable QHash<qint64, std::shared_ptr<QcChunk>> m_chunks;
};

// A key of the cell `cell` for the sets of a walk.
inline quint64 qcSiteCellKey(QcBlockPos cell)
{
    return (static_cast<quint64>(static_cast<quint32>(cell.x)) << 36) ^ (static_cast<quint64>(static_cast<quint32>(cell.z)) << 8) ^ static_cast<quint64>(cell.y);
}

// The feet cells one reaches on foot in `site` from `start` within `bounds`: one step up, three
// down. `passable` and `standable` tell what a block lets one do.
inline QSet<quint64> qcSiteWalk(const QcChunkSite &site, QcBlockPos start, QcXzRect bounds, const std::function<bool(QcBlockId)> &passable,
                                const std::function<bool(QcBlockId)> &standable)
{
    QSet<quint64> seen{qcSiteCellKey(start)};
    std::deque<QcBlockPos> open{start};
    while (!open.empty()) {
        const QcBlockPos feet = open.front();
        open.pop_front();
        for (const auto &[dx, dz] : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) {
            for (const int dy : {1, 0, -1, -2, -3}) {
                const QcBlockPos next = feet.offset(dx, dy, dz);
                if (!bounds.contains(next.x, next.z) || seen.contains(qcSiteCellKey(next))) {
                    continue;
                }
                // Up needs headroom over the cell one leaves, a drop free air down the column.
                bool free = passable(site.at(next)) && passable(site.at(next.offset(0, 1, 0))) && standable(site.at(next.offset(0, -1, 0)));
                free = free && (dy <= 0 || passable(site.at(feet.offset(0, 2, 0))));
                for (int fall = dy + 1; fall <= 0 && free; ++fall) {
                    free = passable(site.at(feet.offset(dx, fall, dz))) && passable(site.at(feet.offset(dx, fall + 1, dz)));
                }
                if (free) {
                    seen.insert(qcSiteCellKey(next));
                    open.push_back(next);
                }
            }
        }
    }
    return seen;
}
