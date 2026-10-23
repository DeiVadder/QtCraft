#include "clientcore/session/QcClientWorld.h"

#include "common/world/QcLightVolume.h"

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

QcClientWorld::Memory QcClientWorld::memory() const
{
    Memory memory;
    memory.chunks = m_chunks.size();
    for (const std::shared_ptr<const QcChunk> &chunk : m_chunks) {
        memory.bytes += static_cast<qint64>(sizeof(QcChunk));
        for (int index = 0; index < QcWorld::sectionCount; ++index) {
            const QcChunkSection &section = chunk->section(index);
            memory.bytes += section.heapBytes();
            memory.palettedSections += section.isUniform() ? 0 : 1;
        }
    }
    return memory;
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

QList<std::pair<QcBlockPos, QcBlockId>> QcClientWorld::setBlocks(const QList<QcMsg::BlockUpdate> &updates)
{
    QList<std::pair<QcBlockPos, QcBlockId>> changed;
    QHash<QcChunkPos, std::shared_ptr<QcChunk>> copies;
    for (const QcMsg::BlockUpdate &update : updates) {
        const auto it = m_chunks.find(update.pos.chunk());
        if (!update.pos.isInsideWorldHeight() || it == m_chunks.end() || !m_content->isValidBlock(update.block)) {
            continue;
        }
        std::shared_ptr<QcChunk> &copy = copies[update.pos.chunk()];
        if (!copy) {
            copy = std::make_shared<QcChunk>(**it);
        }
        const QcBlockId previous = copy->block(update.pos.localX(), update.pos.y, update.pos.localZ());
        copy->setBlock(update.pos.localX(), update.pos.y, update.pos.localZ(), update.block);
        changed.append({update.pos, previous});
    }
    for (auto it = copies.cbegin(); it != copies.cend(); ++it) {
        m_chunks.insert(it.key(), it.value());
    }
    return changed;
}

bool QcClientWorld::mayHaveLightSourceNear(QcBlockPos pos, int reach) const
{
    return QcLightVolume::anySourceNear(*m_content, pos, reach, [this](QcChunkPos chunk) { return m_chunks.value(chunk).get(); });
}

bool QcClientWorld::knowsBlocksOf(const QcChunk &chunk, const QcContentRegistry &content)
{
    for (int i = 0; i < QcWorld::sectionCount; ++i) {
        for (const QcBlockId id : chunk.section(i).palette()) {
            if (!content.isValidBlock(id)) {
                return false;
            }
        }
    }
    return true;
}
