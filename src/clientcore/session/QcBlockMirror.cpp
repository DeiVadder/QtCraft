// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcBlockMirror.h"

#include "clientcore/debug/QcClientLogging.h"

using namespace Qt::StringLiterals;

std::optional<QcBlockMirror::Change> QcBlockMirror::handle(const QcMessage &message)
{
    if (const auto *sync = std::get_if<QcMsg::ContentSync>(&message)) {
        return onContentSync(*sync);
    }
    if (const auto *chunkData = std::get_if<QcMsg::ChunkData>(&message)) {
        return onChunkData(*chunkData);
    }
    if (const auto *unload = std::get_if<QcMsg::ChunkUnload>(&message)) {
        return onChunkUnload(unload->pos);
    }
    if (const auto *update = std::get_if<QcMsg::BlockUpdate>(&message)) {
        if (m_world && !m_content->isValidBlock(update->block)) {
            qCWarning(lcSession) << "Ignoring a block update with the unknown block id" << update->block << "at" << update->pos.x
                                 << update->pos.y << update->pos.z;
            return Change{};
        }
        return onServerBlocks({*update});
    }
    if (const auto *batch = std::get_if<QcMsg::BlockBatch>(&message)) {
        // One copy per chunk; the meshes gather the light changes into one job (decision E-604).
        return onServerBlocks(batch->updates);
    }
    if (const auto *ack = std::get_if<QcMsg::BlockAck>(&message)) {
        return onAck(ack->sequence);
    }
    return std::nullopt;
}

QcBlockMirror::Change QcBlockMirror::onContentSync(const QcMsg::ContentSync &sync)
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

QcBlockMirror::Change QcBlockMirror::onChunkData(const QcMsg::ChunkData &chunkData)
{
    if (!m_world || !QcClientWorld::knowsBlocksOf(*chunkData.chunk, *m_content)) {
        Change refused;
        refused.error = u"Server sent invalid chunk data"_s;
        return refused;
    }

    const QcChunkPos pos = chunkData.chunk->pos();
    m_world->setChunk(chunkData.chunk);

    // The new chunk holds the server's blocks; predicted cells in it keep showing their prediction
    // (the whole chunk is meshed anew anyway, so they are not reported).
    QList<QcMsg::BlockUpdate> predicted;
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
        if (it.key().chunk() == pos) {
            it->server = m_world->blockAt(it.key()).value_or(qcAirBlockId);
            predicted.append({it.key(), it->predicted});
        }
    }
    show(predicted);

    Change change;
    change.loaded = pos;
    return change;
}

QcBlockMirror::Change QcBlockMirror::onChunkUnload(QcChunkPos pos)
{
    Change change;
    if (!m_world) {
        return change;
    }

    m_world->removeChunk(pos);
    m_pending.removeIf([pos](QHash<QcBlockPos, Pending>::iterator entry) { return entry.key().chunk() == pos; });
    change.unloaded = pos;
    return change;
}

QcBlockMirror::Change QcBlockMirror::onServerBlocks(const QList<QcMsg::BlockUpdate> &updates)
{
    Change change;
    if (!m_world) {
        return change;
    }
    if (m_pending.isEmpty()) {
        // Nothing predicted (the usual case): every update shows as it is, without a copy.
        change.blocks = m_world->setBlocks(updates);
        return change;
    }

    QList<QcMsg::BlockUpdate> shown;
    shown.reserve(updates.size());
    for (const QcMsg::BlockUpdate &update : updates) {
        if (const auto pending = m_pending.find(update.pos); pending != m_pending.end()) {
            pending->server = update.block;
        } else {
            shown.append(update);
        }
    }
    change.blocks = m_world->setBlocks(shown);
    return change;
}

QcBlockMirror::Change QcBlockMirror::onAck(quint32 sequence)
{
    Change change;
    QList<QcMsg::BlockUpdate> restored;
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (it->sequence <= sequence) {
            restored.append({it.key(), it->server});
            it = m_pending.erase(it);
        } else {
            ++it;
        }
    }
    change.rolledBack = show(restored);
    return change;
}

QList<QcBlockMirror::Changed> QcBlockMirror::predict(quint32 sequence, const QList<QcMsg::BlockUpdate> &blocks)
{
    if (!m_world) {
        return {};
    }

    QList<QcMsg::BlockUpdate> loaded;
    for (const QcMsg::BlockUpdate &block : blocks) {
        const std::optional<QcBlockId> current = m_world->blockAt(block.pos);
        if (!current || !m_content->isValidBlock(block.block)) {
            continue;
        }
        // A cell predicted again keeps the server's block it remembered.
        Pending &pending = m_pending[block.pos];
        if (pending.sequence == 0) {
            pending.server = *current;
        }
        pending.sequence = sequence;
        pending.predicted = block.block;
        loaded.append(block);
    }
    return show(loaded);
}

std::optional<QcBlockId> QcBlockMirror::serverBlock(QcBlockPos pos) const
{
    if (const auto pending = m_pending.constFind(pos); pending != m_pending.constEnd()) {
        return pending->server;
    }
    return m_world ? m_world->blockAt(pos) : std::nullopt;
}

QList<QcBlockMirror::Changed> QcBlockMirror::show(const QList<QcMsg::BlockUpdate> &blocks)
{
    if (!m_world || blocks.isEmpty()) {
        return {};
    }

    QList<Changed> changed = m_world->setBlocks(blocks);
    changed.removeIf([this](Changed cell) { return m_world->blockAt(cell.first) == cell.second; });
    return changed;
}
