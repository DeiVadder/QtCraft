// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcMailbox.h"
#include "common/core/QcWorkerPool.h"
#include "common/light/QcLightEngine.h"
#include "common/world/QcChunk.h"
#include "server/storage/QcBlockEntityRecord.h"
#include "server/storage/QcChunkStore.h"
#include "server/storage/QcEntityRecord.h"
#include "server/storage/QcLoadWarnings.h"
#include "server/storage/QcRegionFile.h"

#include <QSet>
#include <QStringList>

#include <condition_variable>
#include <functional>

#include <memory>
#include <mutex>
#include <optional>

/*! \brief Reading and writing the chunks of a world in the background (decision E-373), so the
    server tick never waits for the disk: one thread owns the QcChunkStore and runs every request in
    the order it was made, so a chunk read after it was handed over for writing gets what was handed
    over.

    Encoding and decoding happen there too. The tick collects finished reads and saves
    (takeArrivals, takeSaves). Without a directory the store keeps everything in memory. */
class QcChunkIo
{
public:
    /*! \brief A chunk as read: its blocks (none: they were never changed, generate them), what was
        kept of its block entities and entities, and its circuit marks.

        \c {error}: the stored data is damaged; the chunk stays out of the world (decision E-373)
        and its data on disk is left alone. */
    struct Arrival
    {
        /*! The chunk. */
        QcChunkPos pos;
        /*! Which request of the chunk this answers (QcServerWorld drops answers to older ones). */
        quint64 request = 0;
        /*! The blocks; null: never changed, generate them. */
        std::shared_ptr<QcChunk> blocks;
        /*! The blocks were generated for this arrival, not read (the generator's plans for the
            chunk are made, QcFurnishing asks only then). */
        bool generated = false;
        /*! What was kept of its block entities and entities. \qcshared */
        QcBlockEntityRecord blockEntities;
        QcEntityRecord entities;
        /*! Its circuit marks (QcChunkLinks). */
        quint16 marks = 0;
        /*! The warnings of reading it. */
        QStringList warnings;
        /*! Why its stored data is damaged; empty when it is not. */
        QString error;
        /*! The chunk's own light, computed in the background (QcLightEngine::prepare; may be
            null). */
        std::shared_ptr<QcLightEngine::Prepared> light;
    };

    /*! \brief What is kept of a chunk that leaves the world or is saved: its blocks when they
        changed since they were last handed over (none: what is stored stays), its block entities
        and entities (empty records remove what was stored) and its circuit marks. \qcfields */
    struct Departure
    {
        QcChunkPos pos;
        std::shared_ptr<const QcChunk> blocks;
        QcBlockEntityRecord blockEntities;
        QcEntityRecord entities;
        quint16 marks = 0;
    };

    /*! \brief A finished save: the id save() returned and whether it was written. \qcfields */
    struct SaveResult
    {
        quint64 id = 0;
        QcStatus status;
    };

    QcChunkIo(QString directory, std::shared_ptr<const QcContentRegistry> content);
    ~QcChunkIo();
    Q_DISABLE_COPY_MOVE(QcChunkIo)

    /*! Opens the store (before any request; see QcChunkStore::open). Returns what it holds, or
        the error. */
    [[nodiscard]] QcResult<QHash<QcChunkPos, QcChunkStore::Stored>> open();
    /*! Returns the file open() failed on; std::nullopt when it did not. */
    const std::optional<QcChunkStore::Damaged> &damaged() const { return m_store.damaged(); }
    /*! Returns the warnings of open() for the player (QcChunkStore::openWarnings). */
    const QStringList &openWarnings() const { return m_store.openWarnings(); }
    /*! Returns whether the chunks are written to a directory (else kept in memory). */
    bool hasDirectory() const { return !m_store.directory().isEmpty(); }

    /*! Called on the I/O thread with every chunk read in the background that has blocks, before
        it is handed over (the world prepares its light). Set before the first read. */
    using Preparation = std::function<void(Arrival &arrival)>;
    /*! Sets \a prepare as the preparation of every chunk read in the background. */
    void setPreparation(Preparation prepare) { m_prepare = std::move(prepare); }
    /*! Reads the chunk \a pos in the background for \a request; the result comes from
        takeArrivals(). A read goes before the writing work waiting in the queue unless the chunk
        itself is among it. */
    void read(QcChunkPos pos, quint64 request);
    /*! Reads the chunk \a pos and waits for it (spawn, commands, tests). Returns what was read;
        it goes before the waiting writing work unless the chunk is still being handed over, and
        waits only for itself. */
    Arrival readNow(QcChunkPos pos);
    /*! Returns the whole file \a name of the world (a player file) as the latest save handed it
        over or as it is on disk (none: no such file), or the error; waits only for itself. Tick
        thread. */
    [[nodiscard]] QcResult<std::optional<QByteArray>> readFileNow(const QString &name);
    /*! Hands over \a departure, what is kept of a chunk; it is written with the next save. */
    void keep(Departure departure);
    /*! Hands over \a chunks of a save and writes everything handed over so far with \a files,
        the whole files of the save (QcChunkStore::commit). Returns the id the result comes with
        from takeSaves(). */
    quint64 save(QList<Departure> chunks, QList<QcChunkStore::WholeFile> files);
    /*! Waits until every request so far is done. */
    void waitForIdle();
    /*! Returns the chunks handed over and not yet written (as of the last finished request). */
    qsizetype pendingChunks() const;

    /*! Returns the chunks read in the background since the last call; tick thread. */
    std::vector<Arrival> takeArrivals() { return m_arrivals.takeAll(); }
    /*! Returns the saves finished since the last call; tick thread. */
    std::vector<SaveResult> takeSaves();
    /*! Test hook: \a limit is the bytes a chunk's block entities may take
        (QcBlockEntityCodec::encodeWithin; QcRegionFile::maxBlobBytes otherwise). Set before the
        first request. */
    void setBlockEntityLimit(quint32 limit) { m_blockEntityLimit = limit; }
private:
    // The chunk with the warnings of reading it.
    [[nodiscard]] Arrival readChunk(QcChunkPos pos);
    // The chunk as read; its warnings go to m_warnings.
    [[nodiscard]] Arrival decodeChunk(QcChunkPos pos);
    // Runs `job` on the I/O thread (before the queue with `first`) and waits for it alone.
    void runAndWait(std::function<void()> job, bool first);
    void store(const Departure &departure);
    // Puts the block entities of `departure` within m_blockEntityLimit, or keeps what is stored.
    void storeBlockEntities(const Departure &departure);

    std::shared_ptr<const QcContentRegistry> m_content;
    Preparation m_prepare;
    // Used only by the thread of m_pool once it runs (and by open() before).
    QcChunkStore m_store;
    // Warnings of the chunk being read, with the unknown names reported so far (one warning per world
    // and name).
    QcLoadWarnings m_warnings;
    quint32 m_blockEntityLimit = QcRegionFile::maxBlobBytes;
    QcMailbox<Arrival> m_arrivals;
    QcMailbox<SaveResult> m_saves;
    quint64 m_nextSave = 1;
    // The server's tick thread and the storage threads (m_pool) hand the fields below over under
    // this mutex.
    mutable std::mutex m_mutex;
    qsizetype m_pendingChunks = 0;
    // Tick thread: whole files handed over with a save, by name, until a save as new or newer is
    // written.
    struct HandedFile
    {
        quint64 save = 0;
        std::optional<QByteArray> content;
    };
    QHash<QString, HandedFile> m_handedFiles;
    // Chunks handed over by keep() whose job has not run yet, with how many such jobs.
    QHash<QcChunkPos, int> m_keeping;
    // Last member: destroyed first, so no job outlives the store it uses.
    QcWorkerPool m_pool;
};
