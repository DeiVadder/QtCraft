// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/backup/QcChunkRegeneration.h"

#include "common/physics/QcPhysics.h"
#include "server/blocks/QcBlockUpdateQueue.h"
#include "server/blocks/QcFallingBlockManager.h"
#include "server/chunks/QcChunkStreaming.h"
#include "server/core/QcServerState.h"
#include "server/spawners/QcFurnishing.h"

#include <QSet>

#include <algorithm>

QcChunkRegeneration::QcChunkRegeneration(QcServerState &state, QcBlockUpdateQueue &updates)
    : m_state(state)
    , m_updates(updates)
{
}

int QcChunkRegeneration::regenerate(const QList<QcChunkPos> &chunks)
{
    QList<QcChunkPos> done;
    for (const QcChunkPos &chunk : chunks) {
        // A stored chunk that is not loaded comes in first, so its stored copy is replaced too.
        if (!m_state.world().chunk(chunk) && m_state.world().saves().stored(chunk)) {
            m_state.world().loadChunk(chunk);
        }
        if (!m_state.world().regenerateChunk(chunk)) {
            continue;
        }
        // The generated chests, spawners and carts get their loot and kinds again (decisions E-520,
        // E-571); the old carts go with the old blocks.
        removeMinecarts(chunk);
        QcFurnishing::furnish(m_state, chunk);

        dropPendingChanges(chunk);
        resend(chunk);
        done.append(chunk);
    }

    liftPlayers(done);
    removeBuriedCreatures(done);
    return static_cast<int>(done.size());
}

void QcChunkRegeneration::dropPendingChanges(QcChunkPos chunk)
{
    // They belong to the old blocks; the generated ones need none (the clients drop the falling
    // blocks with the next entity sync).
    m_updates.forgetChunk(chunk);
    m_state.liquids().forgetChunk(chunk);
    m_state.circuits().forgetChunk(chunk);
    m_state.hoppers().forgetChunk(chunk);
    m_state.entities().fallingBlocks().removeInChunk(chunk);
    m_state.entities().explosives().removeInChunk(chunk);
}

void QcChunkRegeneration::removeMinecarts(QcChunkPos chunk)
{
    QList<quint32> gone;
    for (const QcMinecart &cart : m_state.entities().minecarts().all()) {
        if (cart.rider == 0 && QcChunkStreaming::chunkOf(cart.motion.position) == chunk) {
            gone.append(cart.id);
        }
    }
    for (const quint32 id : std::as_const(gone)) {
        m_state.entities().minecarts().remove(id);
    }
}

void QcChunkRegeneration::removeBuriedCreatures(const QList<QcChunkPos> &chunks)
{
    const QSet<QcChunkPos> regenerated(chunks.begin(), chunks.end());
    QList<quint32> buried;
    for (const QcCreature &mob : m_state.entities().creatures().mobs()) {
        const QcBlockPos feet = QcPhysics::blockContaining(mob.motion.position);
        if (mob.dormant || !regenerated.contains(feet.chunk())) {
            continue;
        }

        // Every cell from the feet to the top of the body.
        const double height = m_state.content().mobs()[mob.type].height;
        const int top = QcPhysics::blockContaining(mob.motion.position + QcVec3{0.0, std::max(0.0, height - 0.01), 0.0}).y;
        for (QcBlockPos cell = feet; cell.y <= top; ++cell.y) {
            if (!QcFallingBlockManager::isPassable(m_state.world(), cell)) {
                buried.append(mob.id);
                break;
            }
        }
    }

    for (const quint32 id : std::as_const(buried)) {
        m_state.entities().creatures().remove(id);
    }
}

void QcChunkRegeneration::resend(QcChunkPos chunk)
{
    // Streaming sends a chunk again once it is missing from the client's list; the client replaces
    // its copy.
    for (const auto &client : m_state.clients()) {
        if (client->view.sentChunks.remove(chunk)) {
            ++client->view.sentChanges;
        }
    }
}

void QcChunkRegeneration::liftPlayers(const QList<QcChunkPos> &chunks)
{
    const QSet<QcChunkPos> regenerated(chunks.begin(), chunks.end());
    for (const auto &client : m_state.clients()) {
        if (client->state != QcServerClient::State::Playing || !regenerated.contains(QcChunkStreaming::chunkOf(client->motion.position))) {
            continue;
        }

        QcBlockPos feet = QcPhysics::blockContaining(client->motion.position);
        const QcBlockPos start = feet;
        while (feet.y < QcWorld::worldHeight
               && (!QcFallingBlockManager::isPassable(m_state.world(), feet) || !QcFallingBlockManager::isPassable(m_state.world(), feet.offset(0, 1, 0)))) {
            ++feet.y;
        }
        if (feet == start) {
            continue;
        }

        client->motion.position.y = feet.y;
        client->motion.velocity = {};
        client->fallPeak = client->motion.position.y;
        client->previousPosition.reset();
        client->stateDirty = true;
    }
}
