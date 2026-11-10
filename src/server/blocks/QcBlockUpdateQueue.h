// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/core/QcBlockFace.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcChunkPos.h"
#include "server/core/QcServerLimits.h"
#include "server/dimensions/QcPerDimension.h"

#include <QHash>
#include <QList>
#include <QSet>

#include <deque>
#include <optional>

class QcServerState;

/*! \brief Pending neighbour updates (connections, lost support, liquid flow) of the server, in a
    queue of player actions and a queue of commands and their cascades.

    Updates of positions in chunks that are not loaded wait for the chunk. */
class QcBlockUpdateQueue
{
public:
    /*! Updates waiting for their chunk to load at most; loading a chunk also re-checks every
        block on its borders that connects or hangs across them, so dropping beyond this loses
        nothing. Updates waiting for unloaded chunks by default
        (QcServerLimits::waitingBlockUpdates). */
    static constexpr qsizetype maxWaitingBlockUpdates = QcServerLimits{}.waitingBlockUpdates;

    /*! \brief A pending neighbour update. */
    struct Update
    {
        /*! The position to update. */
        QcBlockPos pos;
        /*! It comes from the command queue. */
        bool fromCommand = false;
    };

    explicit QcBlockUpdateQueue(QcServerState &state);

    /*! Returns whether new updates go to the command queue (a command runs or a command update
        cascades). */
    bool commandMode() const { return m_commandMode; }
    /*! Sets command mode \a on or off. */
    void setCommandMode(bool on) { m_commandMode = on; }

    /*! Queues an update of \a pos. */
    void queue(QcBlockPos pos);
    /*! Queues \a pos itself (unless \a self is false) and its six neighbours. */
    void queueNeighbours(QcBlockPos pos, bool self);
    /*! Returns the next update to process, those of player actions first; counts it as
        processed. */
    std::optional<Update> takeNext();
    /*! Remembers an update of \a pos until \a chunk loads. */
    void waitForChunk(QcChunkPos chunk, QcBlockPos pos);
    /*! Re-queues waiting updates of chunks that loaded since the last tick and the blocks along
        their borders that connect or hang across them. */
    void handleLoadedChunks();
    /*! Drops every pending and waiting update inside \a chunk (it was generated again). */
    void forgetChunk(QcChunkPos chunk);
    /*! Returns every pending and waiting update inside \a chunks by chunk and takes them out
        (they leave the world, decision E-374); they come back with their chunk. */
    QHash<QcChunkPos, QList<QcBlockPos>> takeChunks(const QSet<QcChunkPos> &chunks);

    /*! Returns the pending updates of player actions in order. */
    QList<QcBlockPos> playerUpdates() const { return {here().playerUpdates.begin(), here().playerUpdates.end()}; }
    /*! Returns the pending updates of commands in order. */
    QList<QcBlockPos> commandUpdates() const { return {here().commandUpdates.begin(), here().commandUpdates.end()}; }
    /*! Returns the updates waiting for their chunk to load. */
    QList<QcBlockPos> waitingUpdates() const;

    /*! Returns the number of updates of loaded chunks waiting for the next ticks, of player
        actions and of commands. */
    qsizetype pendingCount() const { return pendingPlayerCount() + pendingCommandCount(); }
    /*! Returns the number of pending updates of player actions. */
    qsizetype pendingPlayerCount() const { return static_cast<qsizetype>(here().playerUpdates.size()); }
    /*! Returns the number of pending updates of commands. */
    qsizetype pendingCommandCount() const { return static_cast<qsizetype>(here().commandUpdates.size()); }
    /*! Returns the number of updates waiting for their chunk. */
    qsizetype waitingCount() const { return here().waitingCount; }
    /*! Returns the updates processed since the server started. */
    quint64 processedCount() const { return m_processed; }
    /*! Returns the waiting updates dropped beyond maxWaitingBlockUpdates. */
    quint64 droppedCount() const { return m_dropped; }

private:
    struct BorderScan;

    void queueBorderUpdates(QcChunkPos chunkPos, QcBlockFace towards);
    void queueSectionBorder(BorderScan &scan, int sectionIndex);
    // The rails one column inside the border of a section (their shape can depend on blocks across it).
    void queueInnerRails(const BorderScan &scan, int sectionIndex);
    // Whether a still liquid source on the border can neither flow across nor react there.
    bool stillSourceStays(BorderScan &scan, QcBlockId block, QcBlockPos pos) const;
    bool loneBorderRail(const BorderScan &scan, QcBlockId block, QcBlockPos pos) const;

    QcServerState &m_state;
    bool m_commandMode = false;
    quint64 m_processed = 0;
    quint64 m_dropped = 0;
    // The updates of a dimension.
    struct Queues
    {
        std::deque<QcBlockPos> playerUpdates;
        std::deque<QcBlockPos> commandUpdates;
        QSet<QcBlockPos> queuedPlayerUpdates;
        QSet<QcBlockPos> queuedCommandUpdates;
        // Updates of positions whose chunk was not loaded when their turn came.
        QHash<QcChunkPos, QSet<QcBlockPos>> waitingUpdates;
        qsizetype waitingCount = 0;
    };
    // Of the current dimension.
    Queues &here() const;

    QcPerDimension<Queues> m_queues;
};
