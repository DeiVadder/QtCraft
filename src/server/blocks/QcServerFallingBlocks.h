#pragma once

#include "QcBlockDefinition.h"
#include "QcBlockPos.h"

#include <QSet>

class QcBlockUpdateQueue;
class QcServerBlocks;
class QcServerSounds;
class QcServerState;

// Falling blocks in the server tick (decisions E-055, E-288): a block that loses its hold becomes a
// falling-block entity (QcFallingBlockManager, a kind of the entity register) or, when too many fall,
// drops straight to its resting cell; one that comes to rest becomes its block again (concrete powder
// hardens in water) or breaks into items when the cell is taken or a body is in it.
class QcServerFallingBlocks
{
public:
    QcServerFallingBlocks(QcServerState &state, QcServerSounds &sounds, QcBlockUpdateQueue &updates, QcServerBlocks &blocks);

    // Lets the block at `pos` fall: as an entity, or straight to its resting cell when too many fall.
    // A block that would fall into the void or an unloaded chunk while too many fall waits.
    void start(QcBlockPos pos, QcBlockId block);
    // Queues the waiting falls again once there is room for falling blocks.
    void retryWaiting();
    // Moves the falling blocks and puts those that came to rest.
    void tick();

private:
    // Puts a block that came to rest into its cell, or drops it as items.
    void land(QcBlockId block, QcBlockPos cell, bool placed);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcBlockUpdateQueue &m_updates;
    QcServerBlocks &m_blocks;
    // Blocks that should fall while the falling-block table is full and have no resting cell
    // (decision E-288): they stay in place until it has room again.
    QSet<QcBlockPos> m_waiting;
};
