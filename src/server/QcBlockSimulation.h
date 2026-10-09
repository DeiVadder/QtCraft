#pragma once

#include "QcBlockDefinition.h"
#include "QcBlockPos.h"

class QcBlockUpdateQueue;
class QcServerBlocks;
class QcServerSounds;
struct QcServerState;

// The per-tick block simulation of the server: queued neighbour updates (connections, lost support,
// starting falls), liquid flow and falling blocks.
class QcBlockSimulation
{
public:
    // Neighbour updates (connections, lost support) processed per tick at most; the rest waits for
    // the next tick, so a long chain of falling torches never stalls the server. Updates caused by
    // players go first; those of commands (/fill) and their cascades use what is left.
    static constexpr int maxBlockUpdatesPerTick = 1024;

    QcBlockSimulation(QcServerState &state, QcServerSounds &sounds, QcBlockUpdateQueue &updates, QcServerBlocks &blocks);

    // Re-derives connections and breaks blocks that lost their support, within the tick budget.
    // Positions in unloaded chunks wait until the chunk loads again.
    void processBlockUpdates();
    // Runs the due flow steps of liquids within their budget (decision E-054).
    void processLiquids();
    void tickFallingBlocks();

private:
    void processUpdate(QcBlockPos pos, QcBlockId block);
    void applyFlowStep(QcBlockPos pos, QcBlockId block);
    // Lets the block at `pos` fall: as an entity, or straight to its resting cell when too many fall.
    void startFalling(QcBlockPos pos, QcBlockId block);
    // Puts a block that came to rest into its cell, or drops it as items.
    void landBlock(QcBlockId block, QcBlockPos cell, bool placed);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcBlockUpdateQueue &m_updates;
    QcServerBlocks &m_blocks;
    // Spreading liquids make at most one sound per tick.
    bool m_liquidSoundPlayed = false;
};
