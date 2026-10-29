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

// The blocks of the loaded chunks as the client shows them (decision E-853): what the server's world
// messages say and, on top, the local player's predicted block changes. A prediction sets its cells at
// once and remembers per cell the newest action that set it and the block the server sent there last;
// while a cell waits, the server's blocks for it are only remembered. The acknowledgement of an action
// (QcMsg::BlockAck) ends the waiting of every cell no later action set: the cell shows the server's
// block again, so a refused action is rolled back and a confirmed one changes nothing. The session's
// world mirror (QcWorldMirror) and its tick core (QcSessionCore) each keep one; both see the same
// messages and predictions in the same order.
class QcBlockMirror
{
public:
    // A cell that changed and the block it held before.
    using Changed = std::pair<QcBlockPos, QcBlockId>;

    // What a world message changed.
    struct Change
    {
        // Empty while the message was valid; else why the session closes the connection.
        QString error;
        bool contentReady = false;
        std::optional<QcChunkPos> loaded;
        std::optional<QcChunkPos> unloaded;
        // The blocks the server set.
        QList<Changed> blocks;
        // The cells an acknowledgement rolled back to the server's block.
        QList<Changed> rolledBack;
    };

    // The change a content, chunk, block or acknowledgement message made; none for other messages.
    [[nodiscard]] std::optional<Change> handle(const QcMessage &message);
    // Shows the blocks the action numbered `sequence` is expected to set (cells of chunks that are
    // not loaded are left out) and returns the cells that changed.
    QList<Changed> predict(quint32 sequence, const QList<QcMsg::BlockUpdate> &blocks);

    // Null until the content arrived.
    const QcClientWorld *world() const { return m_world.get(); }
    const std::shared_ptr<const QcContentRegistry> &content() const { return m_content; }
    // Whether the cell shows a prediction that waits for its acknowledgement.
    bool isPredicted(QcBlockPos pos) const { return m_pending.contains(pos); }
    qsizetype predictedCount() const { return m_pending.size(); }
    // The block the server sent last for a cell, also while it shows a prediction; none where no
    // chunk is loaded.
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
    // The server's blocks: those of predicted cells are remembered, the others shown.
    [[nodiscard]] Change onServerBlocks(const QList<QcMsg::BlockUpdate> &updates);
    [[nodiscard]] Change onAck(quint32 sequence);
    // Sets the blocks and returns the cells whose block changed.
    QList<Changed> show(const QList<QcMsg::BlockUpdate> &blocks);

    std::shared_ptr<const QcContentRegistry> m_content;
    std::unique_ptr<QcClientWorld> m_world;
    QHash<QcBlockPos, Pending> m_pending;
};
