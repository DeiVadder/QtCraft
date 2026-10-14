#pragma once

#include "QcBlockDefinition.h"
#include "QcItemStack.h"

#include <QList>

class QcBlockUpdateQueue;
class QcServerSounds;
class QcServerState;

// Block changes of the server with their consequences: clients are told, neighbours queued for an
// update, machines and chests spill their contents, placed saplings start to grow and broken blocks
// drop their items.
class QcServerBlocks
{
public:
    QcServerBlocks(QcServerState &state, QcServerSounds &sounds, QcBlockUpdateQueue &updates);

    // Sets the block, tells clients and queues its neighbours for an update.
    void setAndBroadcast(QcBlockPos pos, QcBlockId block);
    // The same without telling the clients (a piston's block arrives later, decision E-337); false
    // when the block could not be set.
    bool setQuietly(QcBlockPos pos, QcBlockId block);
    // Tells the clients that have the chunk that `pos` shows `block`.
    void sendBlock(QcBlockPos pos, QcBlockId block) const;
    // Removes a block as if broken: drops its items (when `drops` is set) and lets dependent blocks
    // react (leaves decay, unsupported saplings break). The other half of a door breaks with it
    // under the same drop decision.
    void breakBlock(QcBlockPos pos, bool drops);
    // Drops the contents that block entities spilled (QcBlockEntities::takeSpills) as items.
    void dropSpills();
    // Drops the items of block state `block` as if it broke at `pos` (each drop by its chance, only
    // from the states its `when` allows).
    void dropBlockItems(QcBlockId block, QcBlockPos pos);
    void dropStacks(const QList<QcItemStack> &stacks, QcBlockPos pos);

private:
    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcBlockUpdateQueue &m_updates;
};
