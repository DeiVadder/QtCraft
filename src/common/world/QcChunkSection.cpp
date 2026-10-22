#include "common/world/QcChunkSection.h"

#include <QVarLengthArray>

#include <algorithm>

namespace {

// log2 of the indices per word: entries of 1 bit (16 per word) down to 16 bits (one per word).
constexpr unsigned oneBitLog2 = 4;
// Indices of 4 bits, the narrowest width a growing palette uses (repacked at 17 entries).
constexpr unsigned fourBitsLog2 = 2;

// The most indices per word whose bits still hold every index of a palette of `size` entries.
constexpr unsigned perWordLog2For(qsizetype size)
{
    unsigned log2 = oneBitLog2;
    while (log2 > 0 && (qsizetype(1) << (16u >> log2)) < size) {
        --log2;
    }
    return log2;
}

static_assert(perWordLog2For(2) == 4 && perWordLog2For(3) == 3 && perWordLog2For(16) == 2);
static_assert(perWordLog2For(17) == 1 && perWordLog2For(256) == 1 && perWordLog2For(257) == 0);

} // namespace

void QcChunkSection::setBlock(int x, int y, int z, QcBlockId block)
{
    Q_ASSERT(x >= 0 && x < QcWorld::chunkSize && y >= 0 && y < QcWorld::sectionHeight && z >= 0
             && z < QcWorld::chunkSize);

    if (isUniform()) {
        if (m_palette.constFirst() == block) {
            return;
        }
        m_entriesPerWordLog2 = fourBitsLog2;
        m_words.fill(0, QcWorld::blocksPerSection >> fourBitsLog2);
    }

    auto entry = std::ranges::find(m_palette, block);
    if (entry == m_palette.end()) {
        m_palette.append(block);
        entry = m_palette.end() - 1;
        const unsigned needed = perWordLog2For(m_palette.size());
        if (needed < m_entriesPerWordLog2) {
            rewrite({}, std::min(needed, fourBitsLog2));
        }
    }

    const int cell = index(x, y, z);
    const unsigned perWordLog2 = m_entriesPerWordLog2;
    const unsigned bits = 16u >> perWordLog2;
    const unsigned shift = (unsigned(cell) & ((1u << perWordLog2) - 1u)) * bits;
    const unsigned mask = ((1u << bits) - 1u) << shift;
    const auto value = unsigned(entry - m_palette.begin()) << shift;
    quint16 &word = m_words[cell >> perWordLog2];
    word = quint16((unsigned(word) & ~mask) | value);
    m_compacted = false;
}

void QcChunkSection::fill(QcBlockId block)
{
    m_palette = {block};
    // A fresh list: clear() would keep the capacity of the words.
    m_words = QList<quint16>();
}

QList<quint16> QcChunkSection::unpackedIndices() const
{
    if (isUniform()) {
        return {};
    }
    // Word by word: the indices of a word lie in the order of their cells, lowest bits first.
    const unsigned perWord = 1u << m_entriesPerWordLog2;
    const unsigned bits = 16u >> m_entriesPerWordLog2;
    const unsigned mask = (1u << bits) - 1u;
    QList<quint16> indices(QcWorld::blocksPerSection);
    qsizetype cell = 0;
    for (const quint16 word : m_words) {
        unsigned rest = word;
        for (unsigned slot = 0; slot < perWord; ++slot) {
            indices[cell++] = quint16(rest & mask);
            rest >>= bits;
        }
    }
    return indices;
}

void QcChunkSection::pack(const QList<quint16> &indices, unsigned perWordLog2)
{
    Q_ASSERT(indices.size() == QcWorld::blocksPerSection);
    const unsigned bits = 16u >> perWordLog2;
    QList<quint16> words(QcWorld::blocksPerSection >> perWordLog2, 0);
    for (int cell = 0; cell < QcWorld::blocksPerSection; ++cell) {
        const unsigned shift = (unsigned(cell) & ((1u << perWordLog2) - 1u)) * bits;
        words[cell >> perWordLog2] = quint16(words[cell >> perWordLog2] | (unsigned(indices[cell]) << shift));
    }
    m_words = std::move(words);
    m_entriesPerWordLog2 = quint8(perWordLog2);
}

void QcChunkSection::rewrite(std::span<const quint16> remap, unsigned perWordLog2)
{
    const unsigned bits = 16u >> perWordLog2;
    QList<quint16> words(QcWorld::blocksPerSection >> perWordLog2, 0);
    for (int cell = 0; cell < QcWorld::blocksPerSection; ++cell) {
        const quint16 entry = packedIndex(cell);
        const unsigned shift = (unsigned(cell) & ((1u << perWordLog2) - 1u)) * bits;
        words[cell >> perWordLog2] = quint16(words[cell >> perWordLog2] | (unsigned(remap.empty() ? entry : remap[entry]) << shift));
    }
    m_words = std::move(words);
    m_entriesPerWordLog2 = quint8(perWordLog2);
}

bool QcChunkSection::assign(QList<QcBlockId> palette, const QList<quint16> &indices)
{
    if (palette.isEmpty() || palette.size() > QcWorld::blocksPerSection) {
        return false;
    }

    if (palette.size() == 1) {
        if (!indices.isEmpty() && indices.size() != QcWorld::blocksPerSection) {
            return false;
        }
    } else if (indices.size() != QcWorld::blocksPerSection) {
        return false;
    }

    const auto paletteSize = quint16(palette.size());
    if (std::ranges::any_of(indices, [paletteSize](quint16 i) { return i >= paletteSize; })) {
        return false;
    }

    m_palette = std::move(palette);
    m_compacted = false;
    if (m_palette.size() == 1) {
        m_words = QList<quint16>();
    } else {
        pack(indices, perWordLog2For(m_palette.size()));
    }
    return true;
}

bool QcChunkSection::replacePalette(QList<QcBlockId> palette)
{
    if (palette.size() != m_palette.size()) {
        return false;
    }
    m_palette = std::move(palette);
    m_compacted = false;
    return true;
}

void QcChunkSection::compact()
{
    // Unwritten since the last compact(): nothing to do (the save encoder compacts every section).
    if (isUniform() || m_compacted) {
        return;
    }
    m_compacted = true;

    // Which entries the cells use, run by run. The buffers stay on the stack for palettes up to
    // 256 entries, so compacting an already compact section (every save) allocates nothing.
    QVarLengthArray<bool, 256> used(m_palette.size(), false);
    forEachRun([&used](int, quint16 entry) { used[entry] = true; });

    // The used entries keep their order, so a palette without unused or repeated entries keeps its
    // indices. An entry whose block an earlier used entry already holds (several unknown blocks
    // loaded as air) maps to that earlier entry.
    QVarLengthArray<quint16, 256> remap(m_palette.size(), quint16(0));
    QVarLengthArray<QcBlockId, 256> palette;
    for (qsizetype entry = 0; entry < m_palette.size(); ++entry) {
        if (!used[entry]) {
            continue;
        }
        const auto first = std::ranges::find(palette, m_palette[entry]);
        remap[entry] = quint16(first - palette.begin());
        if (first == palette.end()) {
            palette.append(m_palette[entry]);
        }
    }

    if (palette.size() == 1) {
        fill(palette.front());
        return;
    }
    const unsigned perWordLog2 = perWordLog2For(palette.size());
    if (palette.size() == m_palette.size() && perWordLog2 == m_entriesPerWordLog2) {
        return;
    }
    rewrite(remap, perWordLog2);
    m_palette = QList<QcBlockId>(palette.begin(), palette.end());
}
