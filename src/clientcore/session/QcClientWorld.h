#pragma once

#include "QcBlockAccess.h"
#include "QcChunk.h"
#include "QcContentRegistry.h"

#include <QHash>

#include <memory>

// Client-side mirror of the chunks the server sent. Chunks are immutable snapshots; a block update
// replaces the affected chunk with a modified copy so meshing threads never see a change mid-read.
class QcClientWorld : public QcBlockAccess
{
public:
    explicit QcClientWorld(std::shared_ptr<const QcContentRegistry> content);

    [[nodiscard]] std::optional<QcBlockId> blockAt(QcBlockPos pos) const override;
    const QcContentRegistry &content() const override { return *m_content; }
    bool mayHaveLightSourceNear(QcBlockPos pos, int reach) const override;
    std::shared_ptr<const QcContentRegistry> sharedContent() const { return m_content; }

    std::shared_ptr<const QcChunk> chunk(QcChunkPos pos) const { return m_chunks.value(pos); }
    void setChunk(const std::shared_ptr<const QcChunk> &chunk);
    void removeChunk(QcChunkPos pos);
    bool setBlock(QcBlockPos pos, QcBlockId block);
    QList<QcChunkPos> chunkPositions() const { return m_chunks.keys(); }
    qsizetype chunkCount() const { return m_chunks.size(); }

private:
    std::shared_ptr<const QcContentRegistry> m_content;
    QHash<QcChunkPos, std::shared_ptr<const QcChunk>> m_chunks;
};
