#include "QcServerWorld.h"

#include "QcWorldStorage.h"

#include <QThread>

#include <algorithm>

QcServerWorld::QcServerWorld(std::shared_ptr<const QcContentRegistry> content, std::unique_ptr<QcWorldGenerator> generator)
    : m_content(std::move(content))
    , m_generator(std::move(generator))
    // Leave cores for the GUI, render, server and meshing threads.
    , m_generationPool(std::max(1, QThread::idealThreadCount() / 3))
{
}

void QcServerWorld::requestChunk(QcChunkPos pos)
{
    if (m_chunks.contains(pos) || m_pending.contains(pos)) {
        return;
    }
    m_pending.insert(pos);
    m_generationPool.submit([this, pos] { m_generated.push(m_generator->generate(pos)); });
}

QList<QcChunkPos> QcServerWorld::collectGeneratedChunks()
{
    QList<QcChunkPos> collected;
    for (const std::shared_ptr<QcChunk> &chunk : m_generated.takeAll()) {
        const QcChunkPos pos = chunk->pos();
        m_pending.remove(pos);
        // A synchronous load may have won the race; the first chunk stays authoritative.
        if (!m_chunks.contains(pos)) {
            m_chunks.insert(pos, chunk);
            collected.append(pos);
        }
    }
    return collected;
}

std::optional<QcBlockId> QcServerWorld::blockAt(QcBlockPos pos) const
{
    if (pos.y >= QcWorld::worldHeight) {
        return qcAirBlockId;
    }
    const auto it = m_chunks.constFind(pos.chunk());
    if (it == m_chunks.constEnd()) {
        return std::nullopt;
    }
    return (*it)->block(pos.localX(), pos.y, pos.localZ());
}

std::shared_ptr<const QcChunk> QcServerWorld::chunk(QcChunkPos pos) const
{
    return m_chunks.value(pos);
}

std::shared_ptr<const QcChunk> QcServerWorld::loadChunk(QcChunkPos pos)
{
    auto it = m_chunks.find(pos);
    if (it == m_chunks.end()) {
        it = m_chunks.insert(pos, m_generator->generate(pos));
    }
    return *it;
}

void QcServerWorld::unloadChunk(QcChunkPos pos)
{
    if (m_modified.contains(pos)) {
        return;
    }
    m_chunks.remove(pos);
}

bool QcServerWorld::setBlock(QcBlockPos pos, QcBlockId block)
{
    if (!pos.isInsideWorldHeight() || !m_content->isValidBlock(block)) {
        return false;
    }
    auto it = m_chunks.find(pos.chunk());
    if (it == m_chunks.end()) {
        return false;
    }
    if (it->use_count() > 1) {
        *it = std::make_shared<QcChunk>(**it);
    }
    (*it)->setBlock(pos.localX(), pos.y, pos.localZ(), block);
    m_modified.insert(pos.chunk());
    m_dirtyRegions.insert(QcWorldStorage::regionOf(pos.chunk()));
    return true;
}

void QcServerWorld::adoptStoredChunks(const QHash<QcChunkPos, std::shared_ptr<QcChunk>> &chunks)
{
    for (auto it = chunks.constBegin(); it != chunks.constEnd(); ++it) {
        m_chunks.insert(it.key(), it.value());
        m_modified.insert(it.key());
    }
}

void QcServerWorld::markDirty(const QList<QcChunkPos> &chunks)
{
    for (const QcChunkPos &pos : chunks) {
        m_dirtyRegions.insert(QcWorldStorage::regionOf(pos));
    }
}

QHash<QcChunkPos, std::shared_ptr<const QcChunk>> QcServerWorld::takeChunksToSave()
{
    QHash<QcChunkPos, std::shared_ptr<const QcChunk>> chunks;
    for (const QcChunkPos &pos : std::as_const(m_modified)) {
        if (m_dirtyRegions.contains(QcWorldStorage::regionOf(pos))) {
            chunks.insert(pos, m_chunks.value(pos));
        }
    }
    m_dirtyRegions.clear();
    return chunks;
}
