#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/world/QcChunk.h"

#include <QHash>
#include <QList>
#include <QString>

#include <array>
#include <vector>

// The text on the signs of the loaded chunks (decision E-766): up to four lines per sign block,
// filed by chunk so that they are saved, unloaded and loaded with their chunk
// (QcBlockEntityRecord::Table::Signs), like the monster spawners. A sign without text has no entry.
// Kept in step with the blocks by QcServerWorld: a block that is no sign any more loses its text
// (another state of a sign keeps it). Clients learn the text of each sign they see
// (QcMsg::SignText): after its chunk and whenever it changes.
class QcSignTexts
{
public:
    using Lines = QcSignText::Lines;

    struct Entry
    {
        QcBlockPos pos;
        Lines lines;

        bool operator==(const Entry &) const = default;
    };

    explicit QcSignTexts(const QcContentRegistry &content);

    bool isSign(QcBlockId block) const { return std::size_t(block) < m_sign.size() && m_sign[std::size_t(block)].isSign(); }

    // The block at `pos` changed (QcServerWorld::setBlock): no sign, no text.
    void noteBlock(QcBlockPos pos, QcBlockId after);
    // Forgets the texts of a chunk (it left the world or was generated again).
    void forgetChunk(QcChunkPos pos);
    // The entries of a chunk in position order, to be saved; takeChunk() also forgets them.
    [[nodiscard]] QList<Entry> savedOf(QcChunkPos pos) const;
    [[nodiscard]] QList<Entry> takeChunk(QcChunkPos pos);
    // Puts back saved texts where the chunk still has a sign block (others are dropped), cleaned
    // again to that block's line count and line length (QcSignText::cleaned).
    void restore(const QList<Entry> &entries, const QcChunk &chunk);
    // Sets the text of the sign at `pos` (all lines empty: removes it); the caller checked the
    // block and cleaned the lines. A changed text is announced to the clients.
    void set(QcBlockPos pos, const Lines &lines);

    // The lines at `pos`; null for a sign without text.
    const Lines *at(QcBlockPos pos) const;
    bool contains(QcBlockPos pos) const { return m_entries.contains(pos); }
    qsizetype size() const { return m_entries.size(); }
    QList<QcBlockPos> positionsIn(QcChunkPos pos) const { return m_byChunk.value(pos); }
    // Signs whose text changed, appeared or went since the last call, in position order.
    [[nodiscard]] QList<QcBlockPos> takeChanges();
    void clear();

private:
    void remove(QcBlockPos pos);

    // Per block state: its sign definition (no lines: no sign).
    std::vector<QcSignDefinition> m_sign;
    QHash<QcBlockPos, Lines> m_entries;
    QHash<QcChunkPos, QList<QcBlockPos>> m_byChunk;
    QList<QcBlockPos> m_changes;
};
