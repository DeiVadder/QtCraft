#pragma once

#include "QcBlockAccess.h"
#include "QcChunk.h"
#include "QcContentRegistry.h"
#include "QcWorldGenerator.h"

#include <QHash>

#include <memory>

// Authoritative block world of the server. Chunks handed out as const snapshots stay immutable:
// modifying a chunk that is still shared copies it first.
class QcServerWorld : public QcBlockAccess
{
public:
    QcServerWorld(std::shared_ptr<const QcContentRegistry> content, std::unique_ptr<QcWorldGenerator> generator);

    std::optional<QcBlockId> blockAt(QcBlockPos pos) const override;
    const QcContentRegistry &content() const override { return *m_content; }

    std::shared_ptr<const QcChunk> chunk(QcChunkPos pos) const;
    // Returns the chunk, generating it first when needed.
    std::shared_ptr<const QcChunk> loadChunk(QcChunkPos pos);
    void unloadChunk(QcChunkPos pos);
    qsizetype loadedChunkCount() const { return m_chunks.size(); }
    QList<QcChunkPos> loadedChunks() const { return m_chunks.keys(); }

    // Fails for unloaded chunks, positions outside the world height and unknown block ids.
    bool setBlock(QcBlockPos pos, QcBlockId block);

    const QcWorldGenerator &generator() const { return *m_generator; }

private:
    std::shared_ptr<const QcContentRegistry> m_content;
    std::unique_ptr<QcWorldGenerator> m_generator;
    QHash<QcChunkPos, std::shared_ptr<QcChunk>> m_chunks;
};
