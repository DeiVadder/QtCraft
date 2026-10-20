#include "clientcore/session/QcWorldMirror.h"

#include "clientcore/debug/QcClientLogging.h"
#include "common/physics/QcPhysics.h"

using namespace Qt::StringLiterals;

std::optional<QcWorldMirror::Change> QcWorldMirror::handle(const QcMessage &message, const QcVec3 &eye, quint64 now)
{
    if (std::optional<Change> change = handleChunkMessage(message)) {
        return change;
    }
    if (handleSkyMessage(message, eye, now)) {
        return Change{};
    }
    return std::nullopt;
}

std::optional<QcWorldMirror::Change> QcWorldMirror::handleChunkMessage(const QcMessage &message)
{
    if (const auto *sync = std::get_if<QcMsg::ContentSync>(&message)) {
        return onContentSync(*sync);
    }
    if (const auto *chunkData = std::get_if<QcMsg::ChunkData>(&message)) {
        m_extras.chunkArrived(chunkData->chunk->pos());
        return onChunkData(*chunkData);
    }
    if (const auto *unload = std::get_if<QcMsg::ChunkUnload>(&message)) {
        m_extras.removeChunk(unload->pos);
        Change change;
        if (m_world) {
            m_world->removeChunk(unload->pos);
            change.unloaded = unload->pos;
        }
        return change;
    }
    if (const auto *update = std::get_if<QcMsg::BlockUpdate>(&message)) {
        return onBlockUpdate(*update);
    }
    if (const auto *batch = std::get_if<QcMsg::BlockBatch>(&message)) {
        // One copy per chunk; the meshes gather the light changes into one job (decision E-604).
        Change change;
        if (m_world) {
            change.blocks = m_world->setBlocks(batch->updates);
        }
        return change;
    }
    return std::nullopt;
}

bool QcWorldMirror::handleSkyMessage(const QcMessage &message, const QcVec3 &eye, quint64 now)
{
    if (const auto *labels = std::get_if<QcMsg::ChunkLabels>(&message)) {
        m_extras.setLabels(*labels);
    } else if (const auto *spawner = std::get_if<QcMsg::SpawnerDisplay>(&message)) {
        m_extras.setSpawner(*spawner);
    } else if (const auto *explosion = std::get_if<QcMsg::Explosion>(&message)) {
        m_extras.addExplosion(*explosion, now);
    } else if (const auto *time = std::get_if<QcMsg::WorldTime>(&message)) {
        m_worldTime = *time;
    } else if (const auto *weather = std::get_if<QcMsg::Weather>(&message)) {
        m_weather.receive(*weather);
    } else if (const auto *strike = std::get_if<QcMsg::Lightning>(&message)) {
        m_weather.lightning(*strike, eye, now);
    } else {
        return false;
    }
    return true;
}

QcWorldMirror::Change QcWorldMirror::onContentSync(const QcMsg::ContentSync &sync)
{
    if (m_content) {
        // Block ids of loaded chunks would no longer match; content changes need a reconnect.
        Change refused;
        refused.error = u"Server sent content twice"_s;
        return refused;
    }

    m_content = sync.content;
    m_world = std::make_unique<QcClientWorld>(m_content);
    Change change;
    change.contentReady = true;
    return change;
}

QcWorldMirror::Change QcWorldMirror::onChunkData(const QcMsg::ChunkData &chunkData)
{
    if (!m_world || !QcClientWorld::knowsBlocksOf(*chunkData.chunk, *m_content)) {
        Change refused;
        refused.error = u"Server sent invalid chunk data"_s;
        return refused;
    }

    const QcChunkPos pos = chunkData.chunk->pos();
    m_world->setChunk(chunkData.chunk);
    Change change;
    change.loaded = pos;
    return change;
}

QcWorldMirror::Change QcWorldMirror::onBlockUpdate(QcMsg::BlockUpdate update)
{
    Change change;
    const QcBlockId previous = m_world ? m_world->blockAt(update.pos).value_or(qcAirBlockId) : qcAirBlockId;
    if (m_world && !m_content->isValidBlock(update.block)) {
        qCWarning(lcSession) << "Ignoring a block update with the unknown block id" << update.block << "at" << update.pos.x << update.pos.y
                             << update.pos.z;
        return change;
    }
    if (m_world && m_world->setBlock(update.pos, update.block)) {
        change.blocks.append({update.pos, previous});
    }
    return change;
}

void QcWorldMirror::tick(quint64 now)
{
    m_extras.pruneExplosions(now);
    if (m_worldTime.running) {
        // At another tick rate of the server (or frozen) the day runs faster or slower (decision E-249).
        m_worldTime.time += QcPhysics::tickDuration * m_worldTime.speed;
    }
}
