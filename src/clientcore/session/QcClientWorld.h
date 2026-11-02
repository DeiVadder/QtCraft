// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/protocol/QcWorldMessages.h"
#include "common/world/QcBlockAccess.h"
#include "common/world/QcChunk.h"

#include <QHash>

#include <memory>

/*! \brief Client-side mirror of the chunks the server sent.

    Chunks are immutable snapshots; a block update replaces the affected chunk with a modified
    copy so meshing threads never see a change mid-read. */
class QcClientWorld : public QcBlockAccess
{
public:
    /*! \brief What the blocks of the loaded chunks take (memory statistics, decision E-813). */
    struct Memory
    {
        /*! Loaded chunks. */
        qsizetype chunks = 0;
        /*! Sections with more than one block state, which hold an index per cell. */
        qsizetype palettedSections = 0;
        /*! Bytes the chunks and their sections take. */
        qint64 bytes = 0;
    };

    explicit QcClientWorld(std::shared_ptr<const QcContentRegistry> content);

    /*! Returns whether every block of \a chunk is known to \a content (a chunk of the server
        must be). */
    static bool knowsBlocksOf(const QcChunk &chunk, const QcContentRegistry &content);

    [[nodiscard]] std::optional<QcBlockId> blockAt(QcBlockPos pos) const override;
    const QcContentRegistry &content() const override { return *m_content; }
    bool mayHaveLightSourceNear(QcBlockPos pos, int reach) const override;
    /*! Returns the content the world's blocks belong to, shared. */
    std::shared_ptr<const QcContentRegistry> sharedContent() const { return m_content; }

    /*! Returns the chunk at \a pos; null when it is not loaded. */
    std::shared_ptr<const QcChunk> chunk(QcChunkPos pos) const { return m_chunks.value(pos); }
    /*! Takes \a chunk, replacing the one at its position. */
    void setChunk(const std::shared_ptr<const QcChunk> &chunk);
    /*! Forgets the chunk at \a pos. */
    void removeChunk(QcChunkPos pos);
    /*! Sets the cell at \a pos to \a block in a copy of its chunk. Returns false outside the world
        height or a loaded chunk. */
    bool setBlock(QcBlockPos pos, QcBlockId block);
    /*! Sets the blocks of the batch \a updates with one copy per chunk; returns the cells that
        changed with the blocks they held before (cells of chunks not loaded are left out). */
    QList<std::pair<QcBlockPos, QcBlockId>> setBlocks(const QList<QcMsg::BlockUpdate> &updates);
    /*! Returns the positions of the loaded chunks. */
    QList<QcChunkPos> chunkPositions() const { return m_chunks.keys(); }
    /*! Returns the number of loaded chunks. */
    qsizetype chunkCount() const { return m_chunks.size(); }
    /*! Returns what the blocks of the loaded chunks take. */
    Memory memory() const;

private:
    std::shared_ptr<const QcContentRegistry> m_content;
    QHash<QcChunkPos, std::shared_ptr<const QcChunk>> m_chunks;
};
