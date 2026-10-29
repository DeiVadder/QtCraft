// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcChunkPos.h"

#include <QList>

class QcBlockUpdateQueue;
class QcServerState;

// Repairs chunks by generating them again (decision E-246): the blocks become what the world's
// generator (terrain, flat, void or showcase) makes of the chunk, its block entities go with the old
// blocks (QcServerWorld::regenerateChunk: chests and machines lose their contents, saplings and
// circuit memory are forgotten), pending block, liquid, circuit and hopper work and falling blocks
// in it are dropped, minecarts without a rider go and the generated ones (a mine shaft's chest
// minecarts) come again, players get the chunk again, players inside blocks are lifted to free
// space and creatures inside blocks are removed. Other entities stay; /remove clears them.
class QcChunkRegeneration
{
public:
    // Chunks one command regenerates at most.
    static constexpr qsizetype maxChunks = 64;

    QcChunkRegeneration(QcServerState &state, QcBlockUpdateQueue &updates);

    // Returns how many of the chunks were loaded and regenerated; the others are as generated already.
    int regenerate(const QList<QcChunkPos> &chunks);

private:
    // Block, liquid, circuit and hopper work and falling blocks of the old blocks.
    void dropPendingChanges(QcChunkPos chunk);
    // Minecarts in the chunk without a rider (without drops); a ridden one stays with its rider.
    void removeMinecarts(QcChunkPos chunk);
    void resend(QcChunkPos chunk);
    // Moves playing players in regenerated chunks up until feet and head are in passable blocks.
    void liftPlayers(const QList<QcChunkPos> &chunks);
    // Removes creatures whose body now lies in a block that is not passable (without drops).
    void removeBuriedCreatures(const QList<QcChunkPos> &chunks);

    QcServerState &m_state;
    QcBlockUpdateQueue &m_updates;
};
