#pragma once

#include "QcServerLimits.h"
#include "QcBlockDefinition.h"
#include "QcBlockPos.h"


class QcBlockUpdateQueue;
class QcServerBlocks;
class QcServerFallingBlocks;
class QcServerSounds;
class QcServerState;

// The per-tick block simulation of the server: queued neighbour updates (connections, lost support,
// starting falls through QcServerFallingBlocks) and liquid flow.
class QcBlockSimulation
{
public:
    // Neighbour updates (connections, lost support) processed per tick at most by default
    // (QcServerLimits::blockUpdatesPerTick); the rest waits for the next tick, so a long chain of falling torches never stalls the server. Updates caused by
    // players go first; those of commands (/fill) and their cascades use what is left.
    static constexpr int maxBlockUpdatesPerTick = int(QcServerLimits{}.blockUpdatesPerTick);

    QcBlockSimulation(QcServerState &state, QcServerSounds &sounds, QcBlockUpdateQueue &updates, QcServerBlocks &blocks,
                      QcServerFallingBlocks &falling);

    // Re-derives connections and breaks blocks that lost their support, within the tick budget.
    // Positions in unloaded chunks wait until the chunk loads again.
    void processBlockUpdates();
    // Runs the due flow steps of liquids within their budget (decision E-054).
    void processLiquids();

private:
    void processUpdate(QcBlockPos pos, QcBlockId block);
    void applyFlowStep(QcBlockPos pos, QcBlockId block);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcBlockUpdateQueue &m_updates;
    QcServerBlocks &m_blocks;
    QcServerFallingBlocks &m_falling;
    // Spreading liquids make at most one sound per tick.
    bool m_liquidSoundPlayed = false;
};
