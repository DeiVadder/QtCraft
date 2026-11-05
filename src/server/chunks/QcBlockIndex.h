// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcChunk.h"

#include <QHash>
#include <QList>
#include <QSet>

#include <vector>

/*! \brief The exact positions of the loaded blocks of some marked states (Signal Hoppers, decision
    E-344), kept up to date as chunks arrive and blocks are set, so that a tick never scans sections
    for them.

    It also notes the cells whose block changed on or next to an indexed block ("touched"), so
    that sleeping hoppers wake only when something around them changed. */
class QcBlockIndex
{
public:
    QcBlockIndex() = default;
    /*! \a marked is indexed by block state id. */
    explicit QcBlockIndex(std::vector<bool> marked);

    /*! Returns whether the state \a block is indexed. */
    bool isMarked(QcBlockId block) const { return static_cast<std::size_t>(block) < m_marked.size() && m_marked[static_cast<std::size_t>(block)]; }

    /*! Replaces the positions of \a chunk at \a pos that entered the world; only sections whose
        palette holds a marked state are scanned. The new positions count as touched. */
    void noteChunk(QcChunkPos pos, const QcChunk &chunk);
    /*! The block \a block was set at \a pos: adds or removes the position and notes the cell as
        touched when an indexed block is at it or next to it. */
    void noteBlock(QcBlockPos pos, QcBlockId block);
    /*! Forgets the positions of the chunk \a pos that left the world. */
    void forgetChunk(QcChunkPos pos);

    /*! Returns whether an indexed block is at \a pos. */
    bool contains(QcBlockPos pos) const { return m_all.contains(pos); }
    /*! Returns the number of indexed positions. */
    qsizetype size() const { return m_all.size(); }
    /*! Returns the cells touched since the last call. */
    QList<QcBlockPos> takeTouched();

private:
    bool hasNear(QcBlockPos pos) const;

    std::vector<bool> m_marked;
    QHash<QcChunkPos, QList<QcBlockPos>> m_byChunk;
    QSet<QcBlockPos> m_all;
    QSet<QcBlockPos> m_touched;
};
