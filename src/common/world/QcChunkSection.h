#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcWorldConstants.h"

#include <QList>

#include <span>

// 16x16x16 blocks stored as a palette plus per-block palette indices. A section with a single
// block type stores no index array, so empty (all-air) sections cost almost nothing. The indices
// are packed into 16-bit words with as many bits as the palette needs (decision E-844): 4 bits for
// up to 16 entries, 8 bits for up to 256, 16 bits beyond; compact() and assign() also pick 1 or 2
// bits for palettes of 2 or 4 entries. A growing palette widens the indices (repacking all cells
// once); compact() narrows them again. The words are an implicitly shared QList, so a copy of a
// section (a chunk snapshot for mesher and light) shares them until one side writes.
class QcChunkSection
{
public:
    static constexpr int index(int x, int y, int z) { return (y * QcWorld::chunkSize + z) * QcWorld::chunkSize + x; }

    QcBlockId block(int x, int y, int z) const
    {
        Q_ASSERT(x >= 0 && x < QcWorld::chunkSize && y >= 0 && y < QcWorld::sectionHeight && z >= 0
                 && z < QcWorld::chunkSize);
        if (isUniform()) {
            return m_palette.constFirst();
        }
        return m_palette.at(packedIndex(index(x, y, z)));
    }
    void setBlock(int x, int y, int z, QcBlockId block);
    void fill(QcBlockId block);

    bool isUniform() const { return m_words.isEmpty(); }
    bool isEmpty() const { return isUniform() && m_palette.constFirst() == qcAirBlockId; }

    const QList<QcBlockId> &palette() const { return m_palette; }
    // Palette index of the cell `cell` (in the order of index()); 0 in a uniform section.
    quint16 paletteIndex(int cell) const
    {
        Q_ASSERT(cell >= 0 && cell < QcWorld::blocksPerSection);
        return isUniform() ? quint16{0} : packedIndex(cell);
    }
    // Bits per stored palette index: 0 for a uniform section, else 1, 2, 4, 8 or 16.
    int bitsPerIndex() const { return isUniform() ? 0 : 16 >> m_entriesPerWordLog2; }
    // The palette indices of all cells in the order of index() (empty for a uniform section), for
    // encoders and tests; the section itself keeps them packed.
    QList<quint16> unpackedIndices() const;
    // Bytes the palette and the indices hold on the heap (their capacity; shared copies count in
    // full for each holder), for memory statistics.
    qsizetype heapBytes() const
    {
        return m_palette.capacity() * static_cast<qsizetype>(sizeof(QcBlockId)) + m_words.capacity() * static_cast<qsizetype>(sizeof(quint16));
    }

    // Calls `visit(pos)` for every cell whose block is `wanted`, with the section's lowest corner at
    // `corner` in the world.
    template<typename Wanted, typename Visit>
    void forEachCell(QcBlockPos corner, Wanted &&wanted, Visit &&visit) const
    {
        if (isUniform() && !wanted(m_palette.constFirst())) {
            return;
        }
        constexpr int size = QcWorld::chunkSize;
        for (int cell = 0; cell < size * size * QcWorld::sectionHeight; ++cell) {
            if (wanted(m_palette.at(paletteIndex(cell)))) {
                visit(corner.offset(cell % size, cell / (size * size), cell / size % size));
            }
        }
    }
    // Calls `visit(length, paletteIndex)` for each maximal run of equal palette indices in the order
    // of index(); one run of all cells for a uniform section. Compares whole
    // words while a run lasts, so long runs cost one comparison per word (the chunk encoder).
    template<typename Visit>
    void forEachRun(Visit &&visit) const
    {
        if (isUniform()) {
            visit(QcWorld::blocksPerSection, quint16{0});
            return;
        }
        const unsigned perWordLog2 = m_entriesPerWordLog2;
        const unsigned perWord = 1u << perWordLog2;
        const unsigned bits = 16u >> perWordLog2;
        const unsigned mask = (1u << bits) - 1u;
        quint16 current = packedIndex(0);
        int start = 0;
        for (int cell = 0; cell < QcWorld::blocksPerSection; cell += static_cast<int>(perWord)) {
            const unsigned word = m_words.at(cell >> perWordLog2);
            // A word whose every slot holds `current` continues the run.
            if (word == static_cast<unsigned>(current) * (0xFFFFu / mask)) {
                continue;
            }
            for (unsigned slot = 0; slot < perWord; ++slot) {
                const auto entry = static_cast<quint16>((word >> (slot * bits)) & mask);
                if (entry != current) {
                    visit(cell + static_cast<int>(slot) - start, current);
                    current = entry;
                    start = cell + static_cast<int>(slot);
                }
            }
        }
        visit(QcWorld::blocksPerSection - start, current);
    }
    // Replaces the content; returns false when the data is inconsistent (wrong size, index out of palette).
    bool assign(QList<QcBlockId> palette, const QList<quint16> &indices);
    // Replaces the palette's entries by `palette` of the same size, keeping every cell's index (a
    // translation of block ids); returns false for a palette of another size.
    bool replacePalette(QList<QcBlockId> palette);
    // Drops unused palette entries, merges entries of the same block (keeping the first), packs the
    // indices as narrow as the palette allows and collapses to uniform storage when possible. The
    // remaining entries keep their order. Returns at once for a section not written since the last
    // compact().
    void compact();

private:
    quint16 packedIndex(int cell) const
    {
        const unsigned perWordLog2 = m_entriesPerWordLog2;
        const unsigned bits = 16u >> perWordLog2;
        const unsigned slot = static_cast<unsigned>(cell) & ((1u << perWordLog2) - 1u);
        return static_cast<quint16>((static_cast<unsigned>(m_words.at(cell >> perWordLog2)) >> (slot * bits)) & ((1u << bits) - 1u));
    }
    // Packs `indices` (one per cell) with 2^perWordLog2 indices per word into m_words.
    void pack(const QList<quint16> &indices, unsigned perWordLog2);
    // Repacks the current indices with 2^perWordLog2 indices per word, each index i replaced by
    // remap[i] (unchanged when `remap` is empty).
    void rewrite(std::span<const quint16> remap, unsigned perWordLog2);

    QList<QcBlockId> m_palette{qcAirBlockId};
    // Palette indices of all cells, 2^m_entriesPerWordLog2 per word (16, 8, 4, 2 or 1 bits each),
    // cell i in word i >> log2 at bit (i & (2^log2 - 1)) * bits; empty for a uniform section.
    QList<quint16> m_words;
    quint8 m_entriesPerWordLog2 = 2;
    // True from compact() until the next setBlock(), assign() or replacePalette(): the palette has
    // no unused or repeated entries and the indices are as narrow as it allows.
    bool m_compacted = false;
};
