#pragma once

#include "clientcore/session/QcExplosionEffects.h"
#include "common/core/QcBlockPos.h"
#include "common/protocol/QcSignMessages.h"
#include "common/protocol/QcWorldMessages.h"
#include "common/world/QcWorldLabel.h"

#include <QHash>
#include <QList>

#include <algorithm>

// What the server sends about the loaded chunks besides their blocks: the labels floating in them
// (decision E-222), the creature kinds spinning in their monster spawners (decision E-520) and the
// items lying on their campfires (decision E-772). All go with their chunk's ChunkUnload; the
// spawners and campfires also with a new ChunkData of their chunk (the server sends them right after
// its blocks, so a chunk sent again, e.g. after /regenerate, keeps only those it still has). The text
// on signs (decision E-766) comes and goes the same way. The explosions near the player (decision E-604)
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
    // The lines and colour on each sign with text or a colour of the loaded chunks.
    const QHash<QcBlockPos, QcSignText::Text> &signs() const { return m_signs; }
    // Changes whenever a sign's text or colour arrived or left.
    quint64 signGeneration() const { return m_signGeneration; }
    void setSign(const QcMsg::SignText &message)
    {
        const QcSignText::Text text{message.lines, message.color};
        if (QcSignText::isBlank(text)) {
            m_signGeneration += m_signs.remove(message.pos) ? 1 : 0;
            return;
        }
        m_signs.insert(message.pos, text);
        ++m_signGeneration;
    }
    // The items (content ids, 0 for an empty slot) on each campfire of the loaded chunks that holds any.
    const QHash<QcBlockPos, QList<quint32>> &campfires() const { return m_campfires; }
    void setCampfire(const QcMsg::CampfireDisplay &display)
    {
        if (std::ranges::all_of(display.items, [](quint32 item) { return item == 0; })) {
            m_campfires.remove(display.pos);
        } else {
            m_campfires.insert(display.pos, display.items);
        }
    }
    const QcExplosionEffects &explosions() const { return m_explosions; }
    void addExplosion(const QcMsg::Explosion &explosion, quint64 tick) { m_explosions.add(explosion, tick); }
    void pruneExplosions(quint64 tick) { m_explosions.prune(tick); }
    void chunkArrived(QcChunkPos pos)
    {
        removeBlockDisplays(pos);
        removeSigns(pos);
    }
    void removeChunk(QcChunkPos pos)
    {
        m_labelGeneration += m_labels.remove(pos) ? 1 : 0;
        removeBlockDisplays(pos);
        removeSigns(pos);
    }

private:
    void removeBlockDisplays(QcChunkPos pos)
    {
        m_spawners.removeIf([pos](const auto &spawner) { return spawner.key().chunk() == pos; });
        m_campfires.removeIf([pos](const auto &campfire) { return campfire.key().chunk() == pos; });
    }
    void removeSigns(QcChunkPos pos)
    {
        m_signGeneration += m_signs.removeIf([pos](const auto &sign) { return sign.key().chunk() == pos; }) > 0 ? 1 : 0;
    }

    QHash<QcChunkPos, QList<QcWorldLabel>> m_labels;
    quint64 m_labelGeneration = 0;
    QHash<QcBlockPos, int> m_spawners;
    QHash<QcBlockPos, QcSignText::Text> m_signs;
    quint64 m_signGeneration = 0;
    QHash<QcBlockPos, QList<quint32>> m_campfires;
    QcExplosionEffects m_explosions;
};
