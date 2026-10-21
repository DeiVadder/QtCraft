#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/protocol/QcWorldMessages.h"
#include "common/world/QcBlockAccess.h"
#include "common/world/QcChunk.h"

#include <QHash>

#include <memory>

// Client-side mirror of the chunks the server sent. Chunks are immutable snapshots; a block update
// replaces the affected chunk with a modified copy so meshing threads never see a change mid-read.
class QcClientWorld : public QcBlockAccess
{
public:
    // What the blocks of the loaded chunks take (memory statistics, decision E-813).
    struct Memory
    {
        qsizetype chunks = 0;
        // Sections with more than one block state, which hold an index per cell.
        qsizetype palettedSections = 0;
        qint64 bytes = 0;
    };

    explicit QcClientWorld(std::shared_ptr<const QcContentRegistry> content);

    // Whether every block of `chunk` is known to `content` (a chunk of the server must be).
    static bool knowsBlocksOf(const QcChunk &chunk, const QcContentRegistry &content);

    [[nodiscard]] std::optional<QcBlockId> blockAt(QcBlockPos pos) const override;
    const QcContentRegistry &content() const override { return *m_content; }
    bool mayHaveLightSourceNear(QcBlockPos pos, int reach) const override;
    std::shared_ptr<const QcContentRegistry> sharedContent() const { return m_content; }

    std::shared_ptr<const QcChunk> chunk(QcChunkPos pos) const { return m_chunks.value(pos); }
    void setChunk(const std::shared_ptr<const QcChunk> &chunk);
    void removeChunk(QcChunkPos pos);
    bool setBlock(QcBlockPos pos, QcBlockId block);
    // Sets the blocks of a batch with one copy per chunk; returns the cells that changed with the
    // blocks they held before (cells of chunks not loaded are left out).
    QList<std::pair<QcBlockPos, QcBlockId>> setBlocks(const QList<QcMsg::BlockUpdate> &updates);
    QList<QcChunkPos> chunkPositions() const { return m_chunks.keys(); }
    qsizetype chunkCount() const { return m_chunks.size(); }
    Memory memory() const;

private:
    std::shared_ptr<const QcContentRegistry> m_content;
    QHash<QcChunkPos, std::shared_ptr<const QcChunk>> m_chunks;
};
