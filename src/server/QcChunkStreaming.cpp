#include "QcChunkStreaming.h"

#include "QcServerState.h"

#include <algorithm>
#include <cmath>

namespace {

// Bounded so that turning around re-prioritises quickly instead of finishing a long old queue.
constexpr qsizetype maxPendingGeneration = 48;

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
    const int radius = m_state.config.viewDistance;

    // Unload with one chunk of hysteresis so walking along a border does not thrash.
    for (auto it = client.sentChunks.begin(); it != client.sentChunks.end();) {
        if (qcChunkDistance(*it, centre) > radius + 1) {
            client.connection->send(QcMsg::ChunkUnload{*it});
            it = client.sentChunks.erase(it);
        } else {
            ++it;
        }
    }

    QList<QcChunkPos> missing;
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dz = -radius; dz <= radius; ++dz) {
            const QcChunkPos pos{centre.x + dx, centre.z + dz};
            if (!client.sentChunks.contains(pos)) {
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

    int sent = 0;
    for (const QcChunkPos &pos : std::as_const(missing)) {
        if (const std::shared_ptr<const QcChunk> chunk = m_state.world.chunk(pos)) {
            if (sent < m_state.config.chunksPerTick) {
                client.connection->send(QcMsg::ChunkData{chunk});
                client.sentChunks.insert(pos);
                ++sent;
            }
        } else if (m_state.world.pendingGenerationCount() < maxPendingGeneration) {
            // Nearest first: the list is sorted by distance and the pool works in FIFO order.
            m_state.world.requestChunk(pos);
        }
    }
}

void QcChunkStreaming::unloadUnneeded()
{
    QSet<QcChunkPos> needed;
    for (const auto &client : m_state.clients) {
        needed.unite(client->sentChunks);
        const QcChunkPos centre = chunkOf(client->motion.position);
        for (int dx = -m_state.config.viewDistance; dx <= m_state.config.viewDistance; ++dx) {
            for (int dz = -m_state.config.viewDistance; dz <= m_state.config.viewDistance; ++dz) {
                needed.insert({centre.x + dx, centre.z + dz});
            }
        }
    }

    const QList<QcChunkPos> loaded = m_state.world.loadedChunks();
    for (const QcChunkPos &pos : loaded) {
        if (!needed.contains(pos)) {
            m_state.world.unloadChunk(pos);
        }
    }
}
