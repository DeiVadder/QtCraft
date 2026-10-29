// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/gameplay/QcItemStack.h"

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
    [[nodiscard]] bool setQuietly(QcBlockPos pos, QcBlockId block);
    // Tells the clients that have the chunk that `pos` shows `block`.
    void sendBlock(QcBlockPos pos, QcBlockId block) const;
    // Removes a block as if broken: drops its items (when `drops` is set) and lets dependent blocks
    // react (leaves decay, unsupported saplings break). The other half of a door breaks with it
    // under the same drop decision.
    void breakBlock(QcBlockPos pos, bool drops);
    // Drops the contents that block entities spilled (QcBlockEntities::takeSpills) as items.
    void dropSpills();
    // Drops the items of block state `block` as if it broke at `pos` (each drop by its chance, only
    // from the states its `when` allows); ores add their experience (decision E-398). A container or
    // machine with a custom name `name` (decision E-873) drops its item with that name.
    void dropBlockItems(QcBlockId block, QcBlockPos pos, const QString &name = {});
    // The same without experience: blocks an explosion breaks give none (decision E-602).
    void dropItemsWithoutExperience(QcBlockId block, QcBlockPos pos, const QString &name = {});
    void dropStacks(const QList<QcItemStack> &stacks, QcBlockPos pos);

private:
    // Whether `item` places a block that holds a container or machine, so its drop takes the
    // block's custom name (decision E-873).
    bool holdsName(QcItemId item) const;

    QcServerState &m_state;
    QcServerSounds &m_sounds;
    QcBlockUpdateQueue &m_updates;
};
