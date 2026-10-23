#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "common/core/QcBlockFace.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcChunkPos.h"
#include "server/core/QcServerLimits.h"

#include <QHash>
#include <QList>
#include <QSet>

#include <deque>
#include <optional>

class QcServerState;

// Pending neighbour updates (connections, lost support, liquid flow) of the server, in a queue of
// player actions and a queue of commands and their cascades. Updates of positions in chunks that are
// not loaded wait for the chunk.
class QcBlockUpdateQueue
{
public:
    // Updates waiting for their chunk to load at most; loading a chunk also re-checks every block on
    // its borders that connects or hangs across them, so dropping beyond this loses nothing.
    // Updates waiting for unloaded chunks by default (QcServerLimits::waitingBlockUpdates).
    static constexpr qsizetype maxWaitingBlockUpdates = QcServerLimits{}.waitingBlockUpdates;

    struct Update
    {
        QcBlockPos pos;
        bool fromCommand = false;
    };

    explicit QcBlockUpdateQueue(QcServerState &state);

    // While set, new updates go to the command queue (a command runs or a command update cascades).
    bool commandMode() const { return m_commandMode; }
    void setCommandMode(bool on) { m_commandMode = on; }

    void queue(QcBlockPos pos);
    // Queues the position itself (unless `self` is false) and its six neighbours.
    void queueNeighbours(QcBlockPos pos, bool self);
    // The next update to process, those of player actions first; counts it as processed.
    std::optional<Update> takeNext();
    // Remembers an update of `pos` until `chunk` loads.
    void waitForChunk(QcChunkPos chunk, QcBlockPos pos);
    // Re-queues waiting updates of chunks that loaded since the last tick and the blocks along their
    // borders that connect or hang across them.
    void handleLoadedChunks();
    // Drops every pending and waiting update inside the chunk (it was generated again).
    void forgetChunk(QcChunkPos chunk);
    // Takes every pending and waiting update inside these chunks (they leave the world, decision
    // E-374), by chunk; they come back with their chunk.
    QHash<QcChunkPos, QList<QcBlockPos>> takeChunks(const QSet<QcChunkPos> &chunks);

    QList<QcBlockPos> playerUpdates() const { return {m_playerUpdates.begin(), m_playerUpdates.end()}; }
    QList<QcBlockPos> commandUpdates() const { return {m_commandUpdates.begin(), m_commandUpdates.end()}; }
    QList<QcBlockPos> waitingUpdates() const;

    // Updates of loaded chunks waiting for the next ticks, of player actions and of commands.
    qsizetype pendingCount() const { return pendingPlayerCount() + pendingCommandCount(); }
    qsizetype pendingPlayerCount() const { return static_cast<qsizetype>(m_playerUpdates.size()); }
    qsizetype pendingCommandCount() const { return static_cast<qsizetype>(m_commandUpdates.size()); }
    qsizetype waitingCount() const { return m_waitingCount; }
    quint64 processedCount() const { return m_processed; }
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
    std::deque<QcBlockPos> m_playerUpdates;
    std::deque<QcBlockPos> m_commandUpdates;
    QSet<QcBlockPos> m_queuedPlayerUpdates;
    QSet<QcBlockPos> m_queuedCommandUpdates;
    // Updates of positions whose chunk was not loaded when their turn came.
    QHash<QcChunkPos, QSet<QcBlockPos>> m_waitingUpdates;
    qsizetype m_waitingCount = 0;
    bool m_commandMode = false;
    quint64 m_processed = 0;
    quint64 m_dropped = 0;
};
