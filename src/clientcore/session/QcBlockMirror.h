// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/session/QcClientWorld.h"
#include "common/protocol/QcMessages.h"

#include <QHash>
#include <QList>

#include <memory>
#include <optional>
#include <utility>

/*! \brief The blocks of the loaded chunks as the client shows them (decision E-853): what the
    server's world messages say and, on top, the local player's predicted block changes.

    A prediction sets its cells at once and remembers per cell the newest action that set it and
    the block the server sent there last; while a cell waits, the server's blocks for it are only
    remembered. The acknowledgement of an action (QcMsg::BlockAck) ends the waiting of every cell
    no later action set: the cell shows the server's block again, so a refused action is rolled
    back and a confirmed one changes nothing. The session's world mirror (QcWorldMirror) and its
    tick core (QcSessionCore) each keep one; both see the same messages and predictions in the
    same order. */
class QcBlockMirror
{
public:
    /*! A cell that changed and the block it held before. */
    using Changed = std::pair<QcBlockPos, QcBlockId>;

    /*! \brief What a world message changed. */
    struct Change
    {
        /*! Empty while the message was valid; else why the session closes the connection. */
        QString error;
        /*! Whether the message was the content (ContentSync): world() and content() are set now. */
        bool contentReady = false;
        /*! The chunk that arrived, if the message was a ChunkData. */
        std::optional<QcChunkPos> loaded;
        /*! The chunk that left, if the message was a ChunkUnload. */
        std::optional<QcChunkPos> unloaded;
        /*! Whether the message was a DimensionChange: every chunk and prediction is gone
            (decision E-1002). */
        bool dimensionChanged = false;
        /*! The blocks the server set. */
        QList<Changed> blocks;
        /*! The cells an acknowledgement rolled back to the server's block. */
        QList<Changed> rolledBack;
    };

    /*! Returns the change \a message made when it is a content, chunk, block, acknowledgement or
        dimension message; none for other messages. */
    [[nodiscard]] std::optional<Change> handle(const QcMessage &message);
    /*! Shows the \a blocks the action numbered \a sequence is expected to set (cells of chunks that
        are not loaded are left out) and returns the cells that changed. */
    QList<Changed> predict(quint32 sequence, const QList<QcMsg::BlockUpdate> &blocks);

    /*! Returns the blocks of the loaded chunks; null until the content arrived. */
    const QcClientWorld *world() const { return m_world.get(); }
    /*! Returns the content the server sent; null until it arrived. */
    const std::shared_ptr<const QcContentRegistry> &content() const { return m_content; }
    /*! Returns whether the cell \a pos shows a prediction that waits for its acknowledgement. */
    bool isPredicted(QcBlockPos pos) const { return m_pending.contains(pos); }
    /*! Returns the number of cells that show a prediction waiting for its acknowledgement. */
    qsizetype predictedCount() const { return m_pending.size(); }
    /*! Returns the block the server sent last for the cell \a pos, also while it shows a
        prediction; none where no chunk is loaded. */
    [[nodiscard]] std::optional<QcBlockId> serverBlock(QcBlockPos pos) const;

private:
    // A predicted cell waiting for its acknowledgement.
    struct Pending
    {
        // The newest action that set the cell.
        quint32 sequence = 0;
        QcBlockId predicted = qcAirBlockId;
        // The block the server sent last.
        QcBlockId server = qcAirBlockId;
    };

    [[nodiscard]] Change onContentSync(const QcMsg::ContentSync &sync);
    [[nodiscard]] Change onChunkData(const QcMsg::ChunkData &chunkData);
    [[nodiscard]] Change onChunkUnload(QcChunkPos pos);
    [[nodiscard]] Change onDimensionChange();
    // The server's blocks: those of predicted cells are remembered, the others shown.
    [[nodiscard]] Change onServerBlocks(const QList<QcMsg::BlockUpdate> &updates);
    [[nodiscard]] Change onAck(quint32 sequence);
    // Sets the blocks and returns the cells whose block changed.
    QList<Changed> show(const QList<QcMsg::BlockUpdate> &blocks);

    std::shared_ptr<const QcContentRegistry> m_content;
    std::unique_ptr<QcClientWorld> m_world;
    QHash<QcBlockPos, Pending> m_pending;
};
