#include "QcServerWorld.h"

QcServerWorld::QcServerWorld(std::shared_ptr<const QcContentRegistry> content, std::unique_ptr<QcWorldGenerator> generator)
    : m_content(std::move(content))
    , m_generator(std::move(generator))
{
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
    return true;
}
