#include "QcChunkStreaming.h"

#include "QcServerState.h"

#include <QtLogging>

#include <algorithm>
#include <cmath>

namespace {

// Bounded so that turning around re-prioritises quickly instead of finishing a long old queue.
constexpr qsizetype maxPendingGeneration = 48;
// Bytes a network client may have waiting before it gets further chunks.
constexpr qint64 maxPendingChunkBytes = qint64(4) * 1024 * 1024;

} // namespace

QcChunkStreaming::QcChunkStreaming(QcServerState &state)
    : m_state(state)
{
}

QcChunkPos QcChunkStreaming::chunkOf(const QcVec3 &position)
{
    return QcBlockPos{int(std::floor(position.x)), 0, int(std::floor(position.z))}.chunk();
}

void QcChunkStreaming::stream(QcServerClient &client)
{
    const QcChunkPos centre = chunkOf(client.motion.position);
    const int radius = client.view.distance > 0 ? client.view.distance : m_state.config().viewDistance;

    // Unload with one chunk of hysteresis so walking along a border does not thrash.
    for (auto it = client.view.sentChunks.begin(); it != client.view.sentChunks.end();) {
        if (qcChunkDistance(*it, centre) > radius + 1) {
            client.connection->send(QcMsg::ChunkUnload{*it});
            it = client.view.sentChunks.erase(it);
        } else {
            ++it;
        }
    }

    QList<QcChunkPos> missing;
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dz = -radius; dz <= radius; ++dz) {
            const QcChunkPos pos{centre.x + dx, centre.z + dz};
            if (!client.view.sentChunks.contains(pos)) {
                missing.append(pos);
            }
        }
    }

    std::ranges::sort(missing, [centre](QcChunkPos a, QcChunkPos b) {
        const auto distance = [centre](QcChunkPos p) {
            return (p.x - centre.x) * (p.x - centre.x) + (p.z - centre.z) * (p.z - centre.z);
        };
        return distance(a) < distance(b);
    });

    // A slow network client gets new chunks only once most of the sent ones have left (E-251).
    int sent = 0;
    const bool backlog = client.connection->pendingBytes() > maxPendingChunkBytes;
    for (const QcChunkPos &pos : std::as_const(missing)) {
        if (const std::shared_ptr<const QcChunk> chunk = m_state.world().chunk(pos)) {
            if (sent < m_state.config().chunksPerTick && !backlog) {
                client.connection->send(QcMsg::ChunkData{chunk});
                sendLabels(client, pos);
                client.view.sentChunks.insert(pos);
                ++sent;
            }
        } else if (m_state.world().pendingGenerationCount() < maxPendingGeneration) {
            // Nearest first: the list is sorted by distance and the pool works in FIFO order.
            m_state.world().requestChunk(pos);
        }
    }
}

void QcChunkStreaming::sendLabels(QcServerClient &client, QcChunkPos pos) const
{
    QList<QcWorldLabel> labels = m_state.world().generator().labelsIn(pos);
    if (labels.isEmpty()) {
        return;
    }

    // Bounded like the protocol allows, so that no generator can make the client drop the
    // connection; the showcase planner keeps within these limits already.
    if (labels.size() > QcWorldLabel::maxPerChunk) {
        qWarning("chunk %d, %d has %lld labels, only the first %lld are sent", pos.x, pos.z, qlonglong(labels.size()), qlonglong(QcWorldLabel::maxPerChunk));
        labels.resize(QcWorldLabel::maxPerChunk);
    }
    for (QcWorldLabel &label : labels) {
        label = label.limited();
    }
    client.connection->send(QcMsg::ChunkLabels{pos, std::move(labels)});
}

void QcChunkStreaming::unloadUnneeded()
{
    QSet<QcChunkPos> needed;
    for (const auto &client : m_state.clients()) {
        needed.unite(client->view.sentChunks);
        const QcChunkPos centre = chunkOf(client->motion.position);
        const int radius = client->view.distance > 0 ? client->view.distance : m_state.config().viewDistance;
        for (int dx = -radius; dx <= radius; ++dx) {
            for (int dz = -radius; dz <= radius; ++dz) {
                needed.insert({centre.x + dx, centre.z + dz});
            }
        }
    }

    const QList<QcChunkPos> loaded = m_state.world().loadedChunks();
    for (const QcChunkPos &pos : loaded) {
        if (!needed.contains(pos)) {
            m_state.world().unloadChunk(pos);
        }
    }
}
