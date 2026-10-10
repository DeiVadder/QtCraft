#pragma once

#include "QcChunk.h"

#include <QHash>

#include <vector>

// Per loaded chunk, a bit for each section that may hold a block of the marked kinds (contact
// damage, growing plants). A bit is set when its chunk arrives or such a block is placed and only
// cleared when the chunk is noted again or forgotten, so it means "maybe": callers still look at
// the blocks. Chunks without any marked section are missing, so a tick over the marked sections
// costs nothing in a world without such blocks.
class QcSectionMarks
{
public:
    QcSectionMarks() = default;
    // `marked` is indexed by block state id.
    explicit QcSectionMarks(std::vector<bool> marked);

    bool isMarked(QcBlockId block) const { return std::size_t(block) < m_marked.size() && m_marked[std::size_t(block)]; }

    // Replaces the bits of a chunk that entered the world from its section palettes.
    void noteChunk(QcChunkPos pos, const QcChunk &chunk);
    // Sets the bit of the section `block` was placed in, if it is marked.
    void noteBlock(QcBlockPos pos, QcBlockId block);
    void forgetChunk(QcChunkPos pos) { m_sections.remove(pos); }

    bool isEmpty() const { return m_sections.isEmpty(); }
    // The bits of a chunk (bit n: section n), 0 for a chunk without marked sections.
    quint32 sectionsOf(QcChunkPos pos) const { return m_sections.value(pos, 0); }
    // Every chunk with at least one marked section and its bits.
    const QHash<QcChunkPos, quint32> &chunks() const { return m_sections; }

private:
    std::vector<bool> m_marked;
    QHash<QcChunkPos, quint32> m_sections;
};
