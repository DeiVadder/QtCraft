#pragma once

#include "server/storage/QcBlockEntityCodec.h"
#include "server/storage/QcChunkCodec.h"
#include "server/storage/QcChunkStore.h"
#include "server/storage/QcEntityCodec.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <memory>

// Everything stored in a world directory, read the way the server reads it chunk by chunk (stage
// 31.3, decision E-373), for tests that look at saved worlds: blocks, block entity records and
// entity records of every stored chunk.
struct QcStoredWorld
{
    QHash<QcChunkPos, std::shared_ptr<QcChunk>> chunks;
    // Sorted by x, then z.
    QList<QcBlockEntityRecord> blockEntities;
    QList<QcEntityRecord> entities;
    QSet<QcChunkPos> populated;
    QHash<QcChunkPos, quint16> marks;
    QStringList warnings;

    static QcResult<QcStoredWorld> read(const QString &directory, const QcContentRegistry &content)
    {
        QcChunkStore store(directory);
        if (const QcStatus opened = store.open(); !opened) {
            return qcError(opened.error());
        }
        QcStoredWorld world;
        QcLoadWarnings warnings;
        QList<QcChunkPos> positions = store.catalog().keys();
        std::ranges::sort(positions, {}, [](QcChunkPos chunk) { return std::pair{chunk.x, chunk.z}; });
        for (const QcChunkPos &pos : std::as_const(positions)) {
            const QcResult<QcChunkStore::Blobs> blobs = store.read(pos);
            if (!blobs) {
                return qcError(blobs.error());
            }
            if (const QcStatus read = world.add(pos, *blobs, content, warnings); !read) {
                return qcError(read.error());
            }
        }
        world.warnings = warnings.texts();
        return world;
    }

private:
    QcStatus add(QcChunkPos pos, const QcChunkStore::Blobs &blobs, const QcContentRegistry &content, QcLoadWarnings &loadWarnings)
    {
        using Kind = QcChunkStore::Kind;
        marks.insert(pos, blobs.marks);
        if (const auto &data = blobs.data[std::size_t(Kind::Blocks)]) {
            QcResult<std::shared_ptr<QcChunk>> chunk = QcChunkCodec::decode(*data, pos, content, loadWarnings);
            if (!chunk) {
                return qcError(chunk.error());
            }
            chunks.insert(pos, *chunk);
        }
        if (const auto &data = blobs.data[std::size_t(Kind::BlockEntities)]) {
            QcResult<QcBlockEntityRecord> record = QcBlockEntityCodec::decode(*data, pos, content, loadWarnings);
            if (!record) {
                return qcError(record.error());
            }
            if (!record->isEmpty()) {
                blockEntities.append(*record);
            }
        }
        if (const auto &data = blobs.data[std::size_t(Kind::Entities)]) {
            QcResult<QcEntityRecord> record = QcEntityCodec::decode(*data, pos, content, loadWarnings);
            if (!record) {
                return qcError(record.error());
            }
            if (record->populated) {
                populated.insert(pos);
            }
            if (!record->isEmpty()) {
                entities.append(*record);
            }
        }
        return {};
    }
};
