#pragma once

#include "QcChunkPos.h"

#include <QList>

class QcBlockUpdateQueue;
struct QcServerState;

// Repairs chunks by generating them again (decision E-246): the blocks become what the world's
// generator (terrain, flat, void or showcase) makes of the chunk, chests and machines in it lose
// their contents, growing saplings, pending block and liquid updates and falling blocks in it are
// dropped, players get the chunk again, players inside blocks are lifted to free space and
// creatures inside blocks are removed. Other entities stay; /remove clears them.
class QcChunkRegeneration
{
public:
    // Chunks one command regenerates at most.
    static constexpr qsizetype maxChunks = 64;

    QcChunkRegeneration(QcServerState &state, QcBlockUpdateQueue &updates);

    // Returns how many of the chunks were loaded and regenerated; the others are as generated already.
    int regenerate(const QList<QcChunkPos> &chunks);

private:
    void dropBlockEntities(QcChunkPos chunk);
    // Block and liquid updates and falling blocks of the old blocks.
    void dropPendingChanges(QcChunkPos chunk);
    void resend(QcChunkPos chunk);
    // Moves playing players in regenerated chunks up until feet and head are in passable blocks.
    void liftPlayers(const QList<QcChunkPos> &chunks);
    // Removes creatures whose body now lies in a block that is not passable (without drops).
    void removeBuriedCreatures(const QList<QcChunkPos> &chunks);

    QcServerState &m_state;
    QcBlockUpdateQueue &m_updates;
};
