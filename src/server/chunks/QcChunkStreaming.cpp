// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/chunks/QcChunkStreaming.h"

#include "server/blockentities/QcServerSigns.h"
#include "server/chunks/QcChunkLoading.h"
#include "server/core/QcServerLogging.h"
#include "server/core/QcServerState.h"
#include "server/machines/QcServerKitchen.h"
#include "server/spawners/QcSpawnerRules.h"

#include <QtLogging>

#include <algorithm>
#include <cmath>

namespace {

// Bounded so that turning around re-prioritises quickly instead of finishing a long old queue.
constexpr qsizetype maxPendingGeneration = 48;
// Bytes a network client may have waiting before it gets further chunks.
constexpr qint64 maxPendingChunkBytes = qint64{4} * 1024 * 1024;

} // namespace

QcChunkStreaming::QcChunkStreaming(QcServerState &state, QcChunkLoading &loading)
    : m_state(state)
    , m_loading(loading)
{
}

QcChunkPos QcChunkStreaming::chunkOf(const QcVec3 &position)
{
    return QcBlockPos{static_cast<int>(std::floor(position.x)), 0, static_cast<int>(std::floor(position.z))}.chunk();
}

int QcChunkStreaming::radiusOf(const QcServerClient &client) const
{
    return client.view.distance > 0 ? client.view.distance : m_state.config().viewDistance;
}

QList<QcChunkPos> QcChunkStreaming::missingChunks(const QcServerClient &client) const
{
    const QcChunkPos centre = chunkOf(client.motion.position);
    const int radius = radiusOf(client);
    QList<QcChunkPos> missing;
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dz = -radius; dz <= radius; ++dz) {
            const QcChunkPos pos{centre.x + dx, centre.z + dz};
            // No chunk exists beyond the technical bound (decision E-883); the client takes the
            // missing chunks there as a wall.
            if (pos.isInsideWorld() && !client.view.sentChunks.contains(pos)) {
                missing.append(pos);
            }
        }
    }
    return missing;
}

bool QcChunkStreaming::viewComplete(const QcServerClient &client) const
{
    const QcChunkPos centre = chunkOf(client.motion.position);
    const int radius = radiusOf(client);
    for (int dx = -radius; dx <= radius; ++dx) {
        for (int dz = -radius; dz <= radius; ++dz) {
            const QcChunkPos pos{centre.x + dx, centre.z + dz};
            if (pos.isInsideWorld() && !client.view.sentChunks.contains(pos)) {
                return false;
            }
        }
    }
    return true;
}

void QcChunkStreaming::stream(QcServerClient &client)
{
    const QcChunkPos centre = chunkOf(client.motion.position);
    const int radius = radiusOf(client);

    // Unload with one chunk of hysteresis so walking along a border does not thrash.
    for (auto it = client.view.sentChunks.begin(); it != client.view.sentChunks.end();) {
        if (qcChunkDistance(*it, centre) > radius + 1) {
            client.connection->send(QcMsg::ChunkUnload{*it});
            it = client.view.sentChunks.erase(it);
            ++client.view.sentChanges;
        } else {
            ++it;
        }
    }

    QList<QcChunkPos> missing = missingChunks(client);
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
                sendSpawners(client, pos);
                QcServerSigns(m_state).sendChunk(client, pos);
                client.view.sentChunks.insert(pos);
                ++client.view.sentChanges;
                ++sent;
            }
        } else if (m_state.world().pendingGenerationCount() < maxPendingGeneration) {
            // Nearest first: the list is sorted by distance and the pool works in FIFO order.
            m_loading.request(pos);
        }
    }
}

void QcChunkStreaming::sendSpawners(QcServerClient &client, QcChunkPos pos) const
{
    const QList<QcBlockPos> spawners = m_state.world().spawners().positionsIn(pos);
    for (const QcBlockPos &spawner : spawners) {
        client.connection->send(QcSpawnerRules::displayOf(m_state.world(), spawner));
    }
    // And what lies on its campfires (decision E-772).
    const QList<QcBlockPos> campfires = m_state.world().campfires().positionsIn(pos);
    for (const QcBlockPos &campfire : campfires) {
        client.connection->send(QcServerKitchen::displayOf(m_state.world(), campfire));
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
        qCWarning(lcServer, "chunk %d, %d has %lld labels, only the first %lld are sent", pos.x, pos.z, qlonglong(labels.size()), qlonglong(QcWorldLabel::maxPerChunk));
        labels.resize(QcWorldLabel::maxPerChunk);
    }
    for (QcWorldLabel &label : labels) {
        label = label.limited();
    }
    client.connection->send(QcMsg::ChunkLabels{pos, std::move(labels)});
}
