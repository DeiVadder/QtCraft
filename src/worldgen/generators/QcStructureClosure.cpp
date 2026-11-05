// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "worldgen/generators/QcStructureClosure.h"

#include "worldgen/generators/QcWorldGenerator.h"

#include <QSet>

namespace {

// How many chunks are surveyed between two reports (and questions whether to stop).
constexpr qint64 reportEvery = 256;

// The chunks of `columns`.
QcChunkSet chunksOf(QcXzRect columns)
{
    QcChunkSet chunks;
    const QcChunkPos low = QcBlockPos{columns.minX, 0, columns.minZ}.chunk();
    const QcChunkPos high = QcBlockPos{columns.maxX, 0, columns.maxZ}.chunk();
    for (int z = low.z; z <= high.z; ++z) {
        for (int x = low.x; x <= high.x; ++x) {
            chunks.insert(QcChunkPos{x, z});
        }
    }
    return chunks;
}

} // namespace

namespace QcStructureClosure {

std::optional<QcChunkSet> of(const QcWorldGenerator &generator, const QcChunkSet &explored, const QcConversionControl &control)
{
    QcChunkSet kept = explored;
    const QSet<QcChunkPos> chunks = explored.chunks();
    const auto total = static_cast<qint64>(chunks.size());
    qint64 done = 0;
    control.report({QcConversionControl::Stage::Survey, done, total});
    for (const QcChunkPos chunk : chunks) {
        const QList<QcXzRect> reaches = generator.structuresTouching(chunk);
        for (const QcXzRect reach : reaches) {
            kept.unite(chunksOf(reach));
        }
        if (++done % reportEvery == 0) {
            if (control.stopped()) {
                return std::nullopt;
            }
            control.report({QcConversionControl::Stage::Survey, done, total});
        }
    }
    if (control.stopped()) {
        return std::nullopt;
    }
    control.report({QcConversionControl::Stage::Survey, total, total});
    return kept;
}

} // namespace QcStructureClosure
