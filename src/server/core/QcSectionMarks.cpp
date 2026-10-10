#include "QcSectionMarks.h"

#include <algorithm>

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

    if (sections != 0) {
        m_sections.insert(pos, sections);
    } else {
        m_sections.remove(pos);
    }
}

void QcSectionMarks::noteBlock(QcBlockPos pos, QcBlockId block)
{
    if (isMarked(block)) {
        m_sections[pos.chunk()] |= 1u << (pos.y / QcWorld::sectionHeight);
    }
}
