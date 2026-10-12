#pragma once

#include "QcAabb.h"
#include "QcBlockDefinition.h"
#include "QcItemStack.h"
#include "QcMachineManager.h"

#include <QList>

class QcBlockUpdateQueue;
class QcServerSounds;
struct QcServerState;

// Block changes of the server with their consequences: clients are told, neighbours queued for an
// update, machines and chests spill their contents and broken blocks drop their items.
class QcServerBlocks
{
public:
    QcServerBlocks(QcServerState &state, QcServerSounds &sounds, QcBlockUpdateQueue &updates);

    // Sets the block, tells clients and queues its neighbours for an update.
    void setAndBroadcast(QcBlockPos pos, QcBlockId block);
    // Removes a block as if broken: drops its items (when `drops` is set) and lets dependent blocks
    // react (leaves decay, unsupported saplings break). The other half of a door breaks with it
    // under the same drop decision.
    void breakBlock(QcBlockPos pos, bool drops);
    // Opens or closes a door, trapdoor or gate (both halves of a door), unless the changed shape
    // would close on a player or mob.
    void toggle(QcBlockPos pos, QcBlockId block);
    // Whether any of `boxes` (world space) overlaps a playing player or a mob.
    bool overlapsBody(const QList<QcAabb> &boxes) const;
    // Removes chests whose block is no longer a storage block (removed mods) and fits the others to
    // their block's size; spilled contents drop as items.
    void reconcileContainer(QcBlockPos pos);
    // Drops the items of block state `block` as if it broke at `pos` (each drop by its chance, only
    // from the states its `when` allows).
    void dropBlockItems(QcBlockId block, QcBlockPos pos);
    void dropStacks(const QList<QcItemStack> &stacks, QcBlockPos pos);
    void dropMachineContents(const QcMachineManager::Machine &machine);

private:
    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcBlockUpdateQueue &m_updates;
};
