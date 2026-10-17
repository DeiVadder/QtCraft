#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcMailbox.h"
#include "common/core/QcWorkerPool.h"
#include "common/light/QcLightEngine.h"
#include "common/world/QcChunk.h"
#include "server/storage/QcBlockEntityRecord.h"
#include "server/storage/QcChunkStore.h"
#include "server/storage/QcEntityRecord.h"

#include <QSet>
#include <QStringList>

#include <condition_variable>
#include <functional>

#include <memory>
#include <mutex>
#include <optional>

// Reading and writing the chunks of a world in the background (decision E-373), so the server tick
// never waits for the disk: one thread owns the QcChunkStore and runs every request in the order it
// was made, so a chunk read after it was handed over for writing gets what was handed over. Encoding
// and decoding happen there too. The tick collects finished reads and saves (takeArrivals,
// takeSaves). Without a directory the store keeps everything in memory.
class QcChunkIo
{
public:
    // A chunk as read: its blocks (none: they were never changed, generate them), what was kept of its
    // block entities and entities, and its circuit marks. `error`: the stored data is damaged; the
    // chunk stays out of the world (decision E-373) and its data on disk is left alone.
    struct Arrival
    {
        QcChunkPos pos;
        // Which request of the chunk this answers (QcServerWorld drops answers to older ones).
        quint64 request = 0;
        std::shared_ptr<QcChunk> blocks;
        QcBlockEntityRecord blockEntities;
        QcEntityRecord entities;
        quint16 marks = 0;
        QStringList warnings;
        QString error;
        // The chunk's own light, computed in the background (QcLightEngine::prepare; may be null).
        std::shared_ptr<QcLightEngine::Prepared> light;
    };

    // What is kept of a chunk that leaves the world or is saved: its blocks when they changed since
    // they were last handed over (none: what is stored stays), its block entities and entities (empty
    // records remove what was stored) and its circuit marks.
    struct Departure
    {
        QcChunkPos pos;
        std::shared_ptr<const QcChunk> blocks;
        QcBlockEntityRecord blockEntities;
        QcEntityRecord entities;
        quint16 marks = 0;
    };

    struct SaveResult
    {
        quint64 id = 0;
        QcStatus status;
    };

    QcChunkIo(QString directory, std::shared_ptr<const QcContentRegistry> content);
    ~QcChunkIo();

    // Opens the store (before any request; see QcChunkStore::open) and returns what it holds.
    [[nodiscard]] QcResult<QHash<QcChunkPos, QcChunkStore::Stored>> open();
    const std::optional<QcChunkStore::Damaged> &damaged() const { return m_store.damaged(); }
    bool hasDirectory() const { return !m_store.directory().isEmpty(); }

    // Called on the I/O thread with every chunk read in the background that has blocks, before it is
    // handed over (the world prepares its light). Set before the first read.
    using Preparation = std::function<void(Arrival &arrival)>;
    void setPreparation(Preparation prepare) { m_prepare = std::move(prepare); }
    // Reads a chunk in the background; the result comes from takeArrivals(). A read goes before the
    // writing work waiting in the queue unless the chunk itself is among it.
    void read(QcChunkPos pos, quint64 request);
    // Reads a chunk and waits for it (spawn, commands, tests); it goes before the waiting writing
    // work unless the chunk is still being handed over, and waits only for itself.
    Arrival readNow(QcChunkPos pos);
    // A whole file of the world (a player file) as the latest save handed it over or as it is on
    // disk; waits only for itself. Tick thread.
    [[nodiscard]] QcResult<std::optional<QByteArray>> readFileNow(const QString &name);
    // Hands over what is kept of a chunk; it is written with the next save.
    void keep(Departure departure);
    // Hands over the chunks of a save and writes everything handed over so far with the whole files
    // of the save (QcChunkStore::commit); the result comes from takeSaves() with the returned id.
    quint64 save(QList<Departure> chunks, QList<QcChunkStore::WholeFile> files);
    // Waits until every request so far is done.
    void waitForIdle();
    // Chunks handed over and not yet written (as of the last finished request).
    qsizetype pendingChunks() const;

    std::vector<Arrival> takeArrivals() { return m_arrivals.takeAll(); }
    // Finished saves; tick thread.
    std::vector<SaveResult> takeSaves();

private:
    Arrival readChunk(QcChunkPos pos);
    // Runs `job` on the I/O thread (before the queue with `first`) and waits for it alone.
    void runAndWait(std::function<void()> job, bool first);
    void store(const Departure &departure);

    std::shared_ptr<const QcContentRegistry> m_content;
    Preparation m_prepare;
    // Used only by the thread of m_pool once it runs (and by open() before).
    QcChunkStore m_store;
    // Unknown names reported so far (one warning per world and name).
    QSet<QString> m_reported;
    QcMailbox<Arrival> m_arrivals;
    QcMailbox<SaveResult> m_saves;
    quint64 m_nextSave = 1;
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
