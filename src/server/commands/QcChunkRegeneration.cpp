#include "QcChunkRegeneration.h"

#include "QcBlockUpdateQueue.h"
#include "QcChunkStreaming.h"
#include "QcFallingBlockManager.h"
#include "QcPhysics.h"
#include "QcServerState.h"

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
        if (!m_state.world.regenerateChunk(chunk)) {
            continue;
        }

        dropBlockEntities(chunk);
        dropPendingChanges(chunk);
        resend(chunk);
        done.append(chunk);
    }

    liftPlayers(done);
    removeBuriedCreatures(done);
    return int(done.size());
}

void QcChunkRegeneration::dropPendingChanges(QcChunkPos chunk)
{
    // They belong to the old blocks; the generated ones need none (the clients drop the falling
    // blocks with the next entity sync).
    m_updates.forgetChunk(chunk);
    m_state.liquids.forgetChunk(chunk);
    m_state.circuits.forgetChunk(chunk);
    m_state.falling.removeInChunk(chunk);
}

void QcChunkRegeneration::removeBuriedCreatures(const QList<QcChunkPos> &chunks)
{
    const QSet<QcChunkPos> regenerated(chunks.begin(), chunks.end());
    QList<quint32> buried;
    for (const QcMobManager::Mob &mob : m_state.mobs.mobs()) {
        const QcBlockPos feet = QcPhysics::blockContaining(mob.motion.position);
        if (mob.dormant || !regenerated.contains(feet.chunk())) {
            continue;
        }

        // Every cell from the feet to the top of the body.
        const double height = m_state.content->mobs()[mob.type].height;
        const int top = QcPhysics::blockContaining(mob.motion.position + QcVec3{0.0, std::max(0.0, height - 0.01), 0.0}).y;
        for (QcBlockPos cell = feet; cell.y <= top; ++cell.y) {
            if (!QcFallingBlockManager::isPassable(m_state.world, cell)) {
                buried.append(mob.id);
                break;
            }
        }
    }

    for (const quint32 id : std::as_const(buried)) {
        m_state.mobs.remove(id);
    }
}

void QcChunkRegeneration::dropBlockEntities(QcChunkPos chunk)
{
    // The generator's blocks replace them; what they held goes with the changes of the chunk.
    const QList<QcBlockPos> machines = m_state.machines.all().keys();
    for (const QcBlockPos &pos : machines) {
        if (pos.chunk() == chunk) {
            m_state.machines.remove(pos);
        }
    }

    const QList<QcBlockPos> containers = m_state.containers.positions();
    for (const QcBlockPos &pos : containers) {
        if (pos.chunk() == chunk) {
            m_state.containers.remove(pos);
        }
    }

    QList<QcPlantManager::Sapling> saplings = m_state.plants.saplings();
    saplings.removeIf([chunk](const QcPlantManager::Sapling &sapling) { return sapling.pos.chunk() == chunk; });
    m_state.plants.restoreSaplings(saplings);
}

void QcChunkRegeneration::resend(QcChunkPos chunk)
{
    // Streaming sends a chunk again once it is missing from the client's list; the client replaces
    // its copy.
    for (const auto &client : m_state.clients) {
        client->sentChunks.remove(chunk);
    }
}

void QcChunkRegeneration::liftPlayers(const QList<QcChunkPos> &chunks)
{
    const QSet<QcChunkPos> regenerated(chunks.begin(), chunks.end());
    for (const auto &client : m_state.clients) {
        if (client->state != QcServerClient::State::Playing || !regenerated.contains(QcChunkStreaming::chunkOf(client->motion.position))) {
            continue;
        }

        QcBlockPos feet = QcPhysics::blockContaining(client->motion.position);
        const QcBlockPos start = feet;
        while (feet.y < QcWorld::worldHeight
               && (!QcFallingBlockManager::isPassable(m_state.world, feet) || !QcFallingBlockManager::isPassable(m_state.world, feet.offset(0, 1, 0)))) {
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
