#pragma once

#include "QcStoredWorld.h"
#include "server/storage/QcWorldStorage.h"

#include <QDir>

#include <algorithm>

// The saved entities of a world as one list (tests): a record per chunk with any, and the chunks
// that already got their animals.
struct QcWorldEntityRecords
{
    QList<QcEntityRecord> records;
    QSet<QcChunkPos> populated;
};

// A world directory for tests that write and read its chunk data as a whole, all chunks at once
// instead of when needed: world.json and players through QcWorldStorage, blocks, block entities
// and entities through a QcChunkStore and the codecs (decision E-373). Each write replaces what is
// stored of that kind and commits it through one store kept for all writes (like a server's, which
// knows what it wrote); reads open the files afresh.
class QcTestWorldFiles : public QcWorldStorage
{
public:
    QcTestWorldFiles(const QString &directory, std::shared_ptr<const QcContentRegistry> content)
        : QcWorldStorage(directory, content)
        , m_content(std::move(content))
    {
    }

    QcStatus writeChunks(const QHash<QcChunkPos, std::shared_ptr<const QcChunk>> &chunks) const
    {
        QcResult<QcChunkStore *> opened = writer();
        if (!opened) {
            return qcError(opened.error());
        }
        QcChunkStore &store = **opened;
        for (auto it = chunks.cbegin(); it != chunks.cend(); ++it) {
            store.put(QcChunkStore::Kind::Blocks, it.key(), QcChunkCodec::encode(**it, *m_content));
        }
        return store.commit({});
    }

    QcResult<QHash<QcChunkPos, std::shared_ptr<QcChunk>>> readChunks(QStringList &warnings) const
    {
        const QcResult<QcStoredWorld> stored = QcStoredWorld::read(directory(), *m_content);
        if (!stored) {
            return qcError(stored.error());
        }
        warnings.append(stored->warnings);
        return stored->chunks;
    }

    QcStatus writeBlockEntities(const QList<QcBlockEntityRecord> &records) const
    {
        QcResult<QcChunkStore *> opened = writer();
        if (!opened) {
            return qcError(opened.error());
        }
        QcChunkStore &store = **opened;
        const QHash<QcChunkPos, QcChunkStore::Stored> stored = store.catalog();
        for (auto it = stored.cbegin(); it != stored.cend(); ++it) {
            if (it->has(QcChunkStore::Kind::BlockEntities)) {
                store.put(QcChunkStore::Kind::BlockEntities, it.key(), std::nullopt);
            }
        }
        for (const QcBlockEntityRecord &record : records) {
            if (!record.isEmpty()) {
                store.put(QcChunkStore::Kind::BlockEntities, record.chunk, QcBlockEntityCodec::encode(record, *m_content));
            }
        }
        return store.commit({});
    }

    QcResult<QList<QcBlockEntityRecord>> readBlockEntities(QStringList &warnings) const
    {
        m_damaged.clear();
        const QcResult<QcStoredWorld> stored = readWorld();
        if (!stored) {
            return qcError(stored.error());
        }
        warnings.append(stored->warnings);
        return stored->blockEntities;
    }

    QcStatus writeEntities(const QcWorldEntityRecords &entities) const
    {
        QcResult<QcChunkStore *> opened = writer();
        if (!opened) {
            return qcError(opened.error());
        }
        QcChunkStore &store = **opened;
        const QHash<QcChunkPos, QcChunkStore::Stored> stored = store.catalog();
        for (auto it = stored.cbegin(); it != stored.cend(); ++it) {
            if (it->has(QcChunkStore::Kind::Entities)) {
                store.put(QcChunkStore::Kind::Entities, it.key(), std::nullopt);
            }
        }
        QSet<QcChunkPos> chunks = entities.populated;
        for (const QcEntityRecord &record : entities.records) {
            chunks.insert(record.chunk);
        }
        for (const QcChunkPos &chunk : std::as_const(chunks)) {
            QcEntityRecord record;
            if (const auto it = std::ranges::find(entities.records, chunk, &QcEntityRecord::chunk); it != entities.records.end()) {
                record = *it;
            }
            record.chunk = chunk;
            record.populated = entities.populated.contains(chunk);
            store.put(QcChunkStore::Kind::Entities, chunk, QcEntityCodec::encode(record, *m_content));
        }
        return store.commit({});
    }

    QcResult<QcWorldEntityRecords> readEntities(QStringList &warnings) const
    {
        const QcResult<QcStoredWorld> stored = readWorld();
        if (!stored) {
            return qcError(stored.error());
        }
        warnings.append(stored->warnings);
        return QcWorldEntityRecords{stored->entities, stored->populated};
    }

    // blockupdates.qce as a save writes it.
    QcStatus writeBlockUpdates(const QList<QcBlockPos> &positions) const
    {
        const QcResult<std::optional<QByteArray>> bytes = blockUpdatesBytes(positions);
        if (!bytes) {
            return qcError(bytes.error());
        }
        QcResult<QcChunkStore *> opened = writer();
        if (!opened) {
            return qcError(opened.error());
        }
        return (*opened)->commit({{QString::fromLatin1(blockUpdatesFileName), *bytes}});
    }

    // The file (relative to the world directory) the last read failed on, if it was an index.
    QString damagedFile() const { return m_damaged; }

private:
    // The store of all writes, opened with the first.
    QcResult<QcChunkStore *> writer() const
    {
        if (!m_writer) {
            auto store = std::make_unique<QcChunkStore>(directory());
            if (const QcStatus opened = store->open(); !opened) {
                return qcError(opened.error());
            }
            m_writer = std::move(store);
        }
        return m_writer.get();
    }

    QcResult<QcStoredWorld> readWorld() const
    {
        QcChunkStore store(directory());
        if (const QcStatus opened = store.open(); !opened) {
            m_damaged = store.damaged() ? store.damaged()->file : QString();
            return qcError(opened.error());
        }
        return QcStoredWorld::read(directory(), *m_content);
    }

    std::shared_ptr<const QcContentRegistry> m_content;
    mutable QString m_damaged;
    mutable std::unique_ptr<QcChunkStore> m_writer;
};
