#include "QcClientWorld.h"

#include "QcLightVolume.h"


QcClientWorld::QcClientWorld(std::shared_ptr<const QcContentRegistry> content)
    : m_content(std::move(content))
{
}

std::optional<QcBlockId> QcClientWorld::blockAt(QcBlockPos pos) const
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

void QcClientWorld::setChunk(const std::shared_ptr<const QcChunk> &chunk)
{
    m_chunks.insert(chunk->pos(), chunk);
}

void QcClientWorld::removeChunk(QcChunkPos pos)
{
    m_chunks.remove(pos);
}

bool QcClientWorld::setBlock(QcBlockPos pos, QcBlockId block)
{
    if (!pos.isInsideWorldHeight()) {
        return false;
    }
    const auto it = m_chunks.find(pos.chunk());
    if (it == m_chunks.end()) {
        return false;
    }
    auto copy = std::make_shared<QcChunk>(**it);
    copy->setBlock(pos.localX(), pos.y, pos.localZ(), block);
    *it = copy;
    return true;
}

bool QcClientWorld::mayHaveLightSourceNear(QcBlockPos pos, int reach) const
{
    return QcLightVolume::anySourceNear(*m_content, pos, reach, [this](QcChunkPos chunk) { return m_chunks.value(chunk).get(); });
}
