#include "QcChunkSection.h"

#include <algorithm>

QcBlockId QcChunkSection::block(int x, int y, int z) const
{
    Q_ASSERT(x >= 0 && x < QcWorld::chunkSize && y >= 0 && y < QcWorld::sectionHeight && z >= 0
             && z < QcWorld::chunkSize);
    if (isUniform()) {
        return m_palette.constFirst();
    }
    return m_palette[m_indices[index(x, y, z)]];
}

void QcChunkSection::setBlock(int x, int y, int z, QcBlockId block)
{
    Q_ASSERT(x >= 0 && x < QcWorld::chunkSize && y >= 0 && y < QcWorld::sectionHeight && z >= 0
             && z < QcWorld::chunkSize);
    if (isUniform()) {
        if (m_palette.constFirst() == block) {
            return;
        }
        m_indices.fill(0, QcWorld::blocksPerSection);
    }
    auto entry = std::ranges::find(m_palette, block);
    if (entry == m_palette.end()) {
        m_palette.append(block);
        entry = m_palette.end() - 1;
    }
    m_indices[index(x, y, z)] = quint16(entry - m_palette.begin());
}

void QcChunkSection::fill(QcBlockId block)
{
    m_palette = {block};
    m_indices.clear();
}

bool QcChunkSection::assign(QList<QcBlockId> palette, QList<quint16> indices)
{
    if (palette.isEmpty() || palette.size() > QcWorld::blocksPerSection) {
        return false;
    }
    if (palette.size() == 1) {
        if (!indices.isEmpty() && indices.size() != QcWorld::blocksPerSection) {
            return false;
        }
        indices.clear();
    } else if (indices.size() != QcWorld::blocksPerSection) {
        return false;
    }
    const auto paletteSize = quint16(palette.size());
    if (std::ranges::any_of(indices, [paletteSize](quint16 i) { return i >= paletteSize; })) {
        return false;
    }
    m_palette = std::move(palette);
    m_indices = std::move(indices);
    return true;
}

void QcChunkSection::compact()
{
    if (isUniform()) {
        return;
    }
    QList<quint16> remap(m_palette.size(), quint16(0xFFFF));
    QList<QcBlockId> palette;
    for (quint16 &value : m_indices) {
        if (remap[value] == 0xFFFF) {
            remap[value] = quint16(palette.size());
            palette.append(m_palette[value]);
        }
        value = remap[value];
    }
    m_palette = std::move(palette);
    if (m_palette.size() == 1) {
        m_indices.clear();
    }
}
