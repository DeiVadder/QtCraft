#include "QcChunkIo.h"

#include "QcBlockEntityCodec.h"
#include "QcChunkCodec.h"
#include "QcEntityCodec.h"

#include <QDir>
#include <QFile>

using namespace Qt::StringLiterals;

QcChunkIo::QcChunkIo(QString directory, std::shared_ptr<const QcContentRegistry> content)
    : m_content(std::move(content))
    , m_store(std::move(directory))
    , m_pool(1)
{
}

QcChunkIo::~QcChunkIo()
{
    m_pool.waitForIdle();
}

QcResult<QHash<QcChunkPos, QcChunkStore::Stored>> QcChunkIo::open()
{
    if (const QcStatus opened = m_store.open(); !opened) {
        return qcError(opened.error());
    }
    return m_store.catalog();
}

QcChunkIo::Arrival QcChunkIo::readChunk(QcChunkPos pos)
{
    using Kind = QcChunkStore::Kind;
    Arrival arrival;
    arrival.pos = pos;
    const QcResult<QcChunkStore::Blobs> blobs = m_store.read(pos);
    if (!blobs) {
        arrival.error = blobs.error();
        return arrival;
    }
    arrival.marks = blobs->marks;
    const auto &data = blobs->data;
    if (const auto &blocks = data[std::size_t(Kind::Blocks)]) {
        QcResult<std::shared_ptr<QcChunk>> chunk = QcChunkCodec::decode(*blocks, pos, *m_content, arrival.warnings, m_reported);
        if (!chunk) {
            arrival.error = u"blocks of chunk %1, %2: %3"_s.arg(pos.x).arg(pos.z).arg(chunk.error());
            return arrival;
        }
        arrival.blocks = std::move(*chunk);
    }
    if (const auto &entries = data[std::size_t(Kind::BlockEntities)]) {
        QcResult<QcBlockEntityRecord> record = QcBlockEntityCodec::decode(*entries, pos, *m_content, arrival.warnings, m_reported);
        if (!record) {
            arrival.error = record.error();
            return arrival;
        }
        arrival.blockEntities = std::move(*record);
    }
    arrival.blockEntities.chunk = pos;
    if (const auto &entities = data[std::size_t(Kind::Entities)]) {
        QcResult<QcEntityRecord> record = QcEntityCodec::decode(*entities, pos, *m_content, arrival.warnings, m_reported);
        if (!record) {
            arrival.error = record.error();
            return arrival;
        }
        arrival.entities = std::move(*record);
    }
    arrival.entities.chunk = pos;
    return arrival;
}

void QcChunkIo::store(const Departure &departure)
{
    using Kind = QcChunkStore::Kind;
    const QcChunkPos pos = departure.pos;
    if (departure.blocks) {
        m_store.put(Kind::Blocks, pos, QcChunkCodec::encode(*departure.blocks, *m_content));
    }

    // Removing what was never stored would only make the next save look at the region.
    if (!departure.blockEntities.isEmpty() || departure.marks != 0) {
        m_store.put(Kind::BlockEntities, pos, QcBlockEntityCodec::encode(departure.blockEntities, *m_content), departure.marks);
    } else if (m_store.has(Kind::BlockEntities, pos)) {
        m_store.put(Kind::BlockEntities, pos, std::nullopt);
    }
    if (!departure.entities.isEmpty() || departure.entities.populated) {
        m_store.put(Kind::Entities, pos, QcEntityCodec::encode(departure.entities, *m_content));
    } else if (m_store.has(Kind::Entities, pos)) {
        m_store.put(Kind::Entities, pos, std::nullopt);
    }
}

void QcChunkIo::read(QcChunkPos pos, quint64 request)
{
    auto job = [this, pos, request] {
        Arrival arrival = readChunk(pos);
        arrival.request = request;
        m_arrivals.push(std::move(arrival));
    };
    bool behind = false;
    {
        const std::scoped_lock lock(m_mutex);
        behind = m_keeping.contains(pos);
    }
    // A chunk to read must not wait for the encoding of others (a long flight unloads many); one
    // that is still being handed over is read after it, so it gets what was handed over.
    if (behind) {
        m_pool.submit(std::move(job));
    } else {
        m_pool.submitFirst(std::move(job));
    }
}

void QcChunkIo::runAndWait(std::function<void()> job, bool first)
{
    // Waits for this job only, not for the writing work queued around it (review of 31.3).
    struct Done
    {
        std::mutex mutex;
        std::condition_variable signal;
        bool done = false;
    };
    const auto done = std::make_shared<Done>();
    auto wrapped = [done, job = std::move(job)] {
        job();
        const std::scoped_lock lock(done->mutex);
        done->done = true;
        done->signal.notify_all();
    };
    if (first) {
        m_pool.submitFirst(std::move(wrapped));
    } else {
        m_pool.submit(std::move(wrapped));
    }
    std::unique_lock lock(done->mutex);
    done->signal.wait(lock, [&done] { return done->done; });
}

QcChunkIo::Arrival QcChunkIo::readNow(QcChunkPos pos)
{
    bool behind = false;
    {
        const std::scoped_lock lock(m_mutex);
        behind = m_keeping.contains(pos);
    }
    // Before the waiting writing work, unless the chunk is still being handed over.
    Arrival arrival;
    runAndWait([this, pos, &arrival] { arrival = readChunk(pos); }, !behind);
    return arrival;
}

QcResult<std::optional<QByteArray>> QcChunkIo::readFileNow(const QString &name)
{
    if (const auto handed = m_handedFiles.constFind(name); handed != m_handedFiles.cend()) {
        return handed->content;
    }
    // Nothing of this file is handed over (a failed save keeps its files here too): no save in the
    // background writes it, and what is on disk is whole (renamed at once). It is read here, so a
    // join does not wait for a save that runs (review of 31.3).
    QFile file(QDir(m_store.directory()).filePath(name));
    if (m_store.directory().isEmpty() || !file.exists()) {
        return std::optional<QByteArray>();
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return qcError(u"Cannot open %1: %2"_s.arg(file.fileName(), file.errorString()));
    }
    return std::optional<QByteArray>(file.read(qint64(64) * 1024 * 1024));
}

void QcChunkIo::keep(Departure departure)
{
    const QcChunkPos pos = departure.pos;
    {
        const std::scoped_lock lock(m_mutex);
        ++m_keeping[pos];
    }
    m_pool.submit([this, pos, departure = std::move(departure)] {
        store(departure);
        const std::scoped_lock lock(m_mutex);
        m_pendingChunks = m_store.pendingChunks();
        if (--m_keeping[pos] == 0) {
            m_keeping.remove(pos);
        }
    });
}

quint64 QcChunkIo::save(QList<Departure> chunks, QList<QcChunkStore::WholeFile> files)
{
    const quint64 id = m_nextSave++;
    for (const QcChunkStore::WholeFile &file : std::as_const(files)) {
        m_handedFiles.insert(file.name, {id, file.content});
    }
    m_pool.submit([this, id, chunks = std::move(chunks), files = std::move(files)] {
        for (const Departure &departure : chunks) {
            store(departure);
        }
        const QcStatus status = m_store.commit(files);
        {
            const std::scoped_lock lock(m_mutex);
            m_pendingChunks = m_store.pendingChunks();
        }
        m_saves.push({id, status});
    });
    return id;
}

std::vector<QcChunkIo::SaveResult> QcChunkIo::takeSaves()
{
    std::vector<SaveResult> saves = m_saves.takeAll();
    // Files of a save that is written are read from disk again; those of a failed one wait in the
    // store and stay known here until a later save succeeds.
    quint64 written = 0;
    for (const SaveResult &save : saves) {
        if (save.status) {
            written = std::max(written, save.id);
        }
    }
    if (written > 0) {
        m_handedFiles.removeIf([written](const auto &file) { return file.value().save <= written; });
    }
    return saves;
}

void QcChunkIo::waitForIdle()
{
    m_pool.waitForIdle();
}

qsizetype QcChunkIo::pendingChunks() const
{
    const std::scoped_lock lock(m_mutex);
    return m_pendingChunks;
}
