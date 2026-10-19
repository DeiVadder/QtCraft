#include "server/chunks/QcBlockIndex.h"

#include "common/core/QcBlockFace.h"

#include <algorithm>

QcBlockIndex::QcBlockIndex(std::vector<bool> marked)
    : m_marked(std::move(marked))
{
}

void QcBlockIndex::noteChunk(QcChunkPos pos, const QcChunk &chunk)
{
    forgetChunk(pos);

    QList<QcBlockPos> found;
    for (int index = 0; index < QcWorld::sectionCount; ++index) {
        const QcChunkSection &section = chunk.section(index);
        if (std::ranges::any_of(section.palette(), [this](QcBlockId id) { return isMarked(id); })) {
            const QcBlockPos corner{pos.x * QcWorld::chunkSize, index * QcWorld::sectionHeight, pos.z * QcWorld::chunkSize};
            section.forEachCell(corner, [this](QcBlockId id) { return isMarked(id); }, [&found](QcBlockPos block) { found.append(block); });
        }
    }

    if (found.isEmpty()) {
        return;
    }
    for (const QcBlockPos &block : std::as_const(found)) {
        m_all.insert(block);
        m_touched.insert(block);
    }
    m_byChunk.insert(pos, found);
}

void QcBlockIndex::noteBlock(QcBlockPos pos, QcBlockId block)
{
    const bool marked = isMarked(block);
    if (marked && !m_all.contains(pos)) {
        m_all.insert(pos);
        m_byChunk[pos.chunk()].append(pos);
    } else if (!marked && m_all.remove(pos)) {
        const auto chunk = m_byChunk.find(pos.chunk());
        chunk->removeOne(pos);
        if (chunk->isEmpty()) {
            m_byChunk.erase(chunk);
        }
    }

    if (marked || hasNear(pos)) {
        m_touched.insert(pos);
    }
}

bool QcBlockIndex::hasNear(QcBlockPos pos) const
{
    if (m_all.isEmpty()) {
        return false;
    }

    // Most changes happen far from any indexed block: a look at the chunks first.
    bool chunkNear = false;
    for (const int dx : {-1, 0, 1}) {
        for (const int dz : {-1, 0, 1}) {
            chunkNear = chunkNear || m_byChunk.contains(pos.offset(dx, 0, dz).chunk());
        }
    }
    if (!chunkNear) {
        return false;
    }
    return std::ranges::any_of(qcAllBlockFaces, [this, pos](QcBlockFace face) { return m_all.contains(qcNeighbour(pos, face)); });
}

void QcBlockIndex::forgetChunk(QcChunkPos pos)
{
    const QList<QcBlockPos> positions = m_byChunk.take(pos);
    for (const QcBlockPos &block : positions) {
        m_all.remove(block);
    }
}

QList<QcBlockPos> QcBlockIndex::takeTouched()
{
    QList<QcBlockPos> touched(m_touched.begin(), m_touched.end());
    m_touched.clear();
    return touched;
}
