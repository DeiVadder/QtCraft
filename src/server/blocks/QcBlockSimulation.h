#pragma once

#include "common/content/definitions/QcBlockDefinition.h"
#include "common/core/QcBlockPos.h"
#include "server/blocks/QcFireRules.h"
#include "server/core/QcServerLimits.h"


class QcBlockUpdateQueue;
class QcFireIgnition;
class QcServerBlocks;
class QcServerFallingBlocks;
class QcServerSounds;
class QcServerState;

// The per-tick block simulation of the server: queued neighbour updates (connections, lost support,
// starting falls through QcServerFallingBlocks, fire that lost its place going out) and liquid flow.
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
    // Lava that gets a block update may light fire next to it (decision E-393).
    void setIgnition(QcFireIgnition *ignition) { m_ignition = ignition; }

private:
    void processUpdate(QcBlockPos pos, QcBlockId block);
    void applyFlowStep(QcBlockPos pos, QcBlockId block);

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcBlockUpdateQueue &m_updates;
    QcServerBlocks &m_blocks;
    QcServerFallingBlocks &m_falling;
    QcFireRules::Table m_fireTable;
    QcFireIgnition *m_ignition = nullptr;
    // Spreading liquids make at most one sound per tick.
    bool m_liquidSoundPlayed = false;
};
