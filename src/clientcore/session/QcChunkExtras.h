#pragma once

#include "clientcore/session/QcExplosionEffects.h"
#include "common/core/QcBlockPos.h"
#include "common/protocol/QcWorldMessages.h"
#include "common/world/QcWorldLabel.h"

#include <QHash>
#include <QList>

// What the server sends about the loaded chunks besides their blocks: the labels floating in them
// (decision E-222) and the creature kinds spinning in their monster spawners (decision E-520). Both
// go with their chunk's ChunkUnload; the spawners also with a new ChunkData of their chunk (the server
// sends the spawners of a chunk right after its blocks, so a chunk sent again, e.g. after
// /regenerate, keeps only the spawners it still has). The explosions near the player (decision E-604)
// stay as puffs of flash and smoke until they are over.
class QcChunkExtras
{
public:
    const QHash<QcChunkPos, QList<QcWorldLabel>> &labels() const { return m_labels; }
    // Changes whenever a chunk's labels arrived or left.
    quint64 labelGeneration() const { return m_labelGeneration; }
    // The kind (content index) shown in each spawner of the loaded chunks.
    const QHash<QcBlockPos, int> &spawners() const { return m_spawners; }

    void setLabels(const QcMsg::ChunkLabels &labels)
    {
        m_labels.insert(labels.pos, labels.labels);
        ++m_labelGeneration;
    }
    void setSpawner(QcMsg::SpawnerDisplay display)
    {
        if (display.mob < 0) {
            m_spawners.remove(display.pos);
        } else {
            m_spawners.insert(display.pos, display.mob);
        }
    }
    const QcExplosionEffects &explosions() const { return m_explosions; }
    void addExplosion(const QcMsg::Explosion &explosion, quint64 tick) { m_explosions.add(explosion, tick); }
    void pruneExplosions(quint64 tick) { m_explosions.prune(tick); }
    void chunkArrived(QcChunkPos pos) { removeSpawners(pos); }
    void removeChunk(QcChunkPos pos)
    {
        m_labelGeneration += m_labels.remove(pos) ? 1 : 0;
        removeSpawners(pos);
    }

private:
    void removeSpawners(QcChunkPos pos)
    {
        m_spawners.removeIf([pos](const auto &spawner) { return spawner.key().chunk() == pos; });
    }

    QHash<QcChunkPos, QList<QcWorldLabel>> m_labels;
    quint64 m_labelGeneration = 0;
    QHash<QcBlockPos, int> m_spawners;
    QcExplosionEffects m_explosions;
};
