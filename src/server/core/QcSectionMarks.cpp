#include "QcSectionMarks.h"

#include <algorithm>
#include <utility>

static_assert(QcWorld::sectionCount <= 32, "one bit per section in a quint32");

QcSectionMarks::QcSectionMarks(std::vector<bool> marked)
    : m_marked(std::move(marked))
{
}

void QcSectionMarks::noteChunk(QcChunkPos pos, const QcChunk &chunk)
{
    quint32 sections = 0;
    for (int index = 0; index < QcWorld::sectionCount; ++index) {
        if (std::ranges::any_of(chunk.section(index).palette(), [this](QcBlockId id) { return isMarked(id); })) {
            sections |= 1u << index;
        }
    }

    const qsizetype before = m_sections.size();
    if (sections != 0) {
        m_sections.insert(pos, sections);
    } else {
        m_sections.remove(pos);
    }
    m_orderStale = m_orderStale || m_sections.size() != before;
}

void QcSectionMarks::noteBlock(QcBlockPos pos, QcBlockId block)
{
    if (isMarked(block)) {
        const qsizetype before = m_sections.size();
        m_sections[pos.chunk()] |= 1u << (pos.y / QcWorld::sectionHeight);
        m_orderStale = m_orderStale || m_sections.size() != before;
    }
}

void QcSectionMarks::forgetChunk(QcChunkPos pos)
{
    m_orderStale = m_sections.remove(pos) || m_orderStale;
}

const QList<QcChunkPos> &QcSectionMarks::orderedChunks() const
{
    if (m_orderStale) {
        m_order = m_sections.keys();
        std::ranges::sort(m_order, [](QcChunkPos a, QcChunkPos b) { return std::pair{a.x, a.z} < std::pair{b.x, b.z}; });
        m_orderStale = false;
    }
    return m_order;
}
