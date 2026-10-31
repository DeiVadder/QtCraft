// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/world/QcChunk.h"

#include <QHash>
#include <QList>
#include <QString>

#include <array>
#include <vector>

/*! \brief The text on the signs of the loaded chunks (decision E-766): up to four lines per sign
    block, filed by chunk so that they are saved, unloaded and loaded with their chunk
    (QcBlockEntityRecord::Table::Signs), like the monster spawners.

    A sign also keeps the colour a dye gave its text (decision E-830); a sign without text in the
    default colour has no entry. Kept in step with the blocks by QcServerWorld: a block that is
    no sign any more loses its text (another state of a sign keeps it). Clients learn the text of
    each sign they see (QcMsg::SignText): after its chunk and whenever it changes. */
class QcSignTexts
{
public:
    /*! The lines of a sign's text (QcSignText::Lines). */
    using Lines = QcSignText::Lines;

    /*! \brief The text of one sign. */
    struct Entry
    {
        /*! The position of the sign block. */
        QcBlockPos pos;
        /*! The lines of its text. */
        Lines lines;
        /*! QcSignText::Text::color: empty for the default. */
        QString color;

        bool operator==(const Entry &) const = default;
    };

    explicit QcSignTexts(const QcContentRegistry &content);

    /*! Returns whether \a block is a sign state. */
    bool isSign(QcBlockId block) const { return static_cast<std::size_t>(block) < m_sign.size() && m_sign[static_cast<std::size_t>(block)].isSign(); }

    /*! The block at \a pos changed to \a after (QcServerWorld::setBlock): no sign, no text. */
    void noteBlock(QcBlockPos pos, QcBlockId after);
    /*! Forgets the texts of the chunk at \a pos (it left the world or was generated again). */
    void forgetChunk(QcChunkPos pos);
    /*! Returns the entries of the chunk at \a pos in position order, to be saved. */
    [[nodiscard]] QList<Entry> savedOf(QcChunkPos pos) const;
    /*! Returns the entries of the chunk at \a pos like savedOf() and forgets them. */
    [[nodiscard]] QList<Entry> takeChunk(QcChunkPos pos);
    /*! Puts back the saved \a entries where \a chunk still has a sign block (others are dropped),
        cleaned again to that block's line count and line length (QcSignText::cleaned); an
        unknown colour becomes the default. */
    void restore(const QList<Entry> &entries, const QcChunk &chunk);
    /*! Sets the text of the sign at \a pos to \a lines and keeps its colour; the caller checked
        the block and cleaned the lines. A changed text is announced to the clients. */
    void set(QcBlockPos pos, const Lines &lines);
    /*! Sets the \a color of the text of the sign at \a pos (QcSignText::colorOfDye) and keeps its
        lines; the caller checked the block. A changed colour is announced to the clients. */
    void setColor(QcBlockPos pos, const QString &color);

    /*! Returns the lines at \a pos; null for a sign without an entry. */
    const Lines *at(QcBlockPos pos) const;
    /*! Returns the colour of the text at \a pos; empty for the default. */
    QString colorAt(QcBlockPos pos) const;
    /*! Returns whether the sign at \a pos has an entry. */
    bool contains(QcBlockPos pos) const { return m_entries.contains(pos); }
    /*! Returns the number of entries. */
    qsizetype size() const { return m_entries.size(); }
    /*! Returns the positions of the entries in the chunk at \a pos. */
    QList<QcBlockPos> positionsIn(QcChunkPos pos) const { return m_byChunk.value(pos); }
    /*! Returns the signs whose text changed, appeared or went since the last call, in position
        order. */
    [[nodiscard]] QList<QcBlockPos> takeChanges();
    /*! Forgets every text. */
    void clear();

private:
    void remove(QcBlockPos pos);
    void put(QcBlockPos pos, const QcSignText::Text &text);

    // Per block state: its sign definition (no lines: no sign).
    std::vector<QcSignDefinition> m_sign;
    QHash<QcBlockPos, QcSignText::Text> m_entries;
    QHash<QcChunkPos, QList<QcBlockPos>> m_byChunk;
    QList<QcBlockPos> m_changes;
};
