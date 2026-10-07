#pragma once

#include "QcBlockAccess.h"
#include "QcChunk.h"
#include "QcContentRegistry.h"
#include "QcMailbox.h"
#include "QcWorkerPool.h"
#include "QcWorldGenerator.h"

#include <QHash>
#include <QSet>

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
    // Returns the chunk, generating it synchronously when needed (spawn, tests).
    std::shared_ptr<const QcChunk> loadChunk(QcChunkPos pos);
    // Queues background generation unless the chunk is loaded or already queued.
    void requestChunk(QcChunkPos pos);
    // Moves finished background chunks into the world; returns their positions.
    QList<QcChunkPos> collectGeneratedChunks();
    qsizetype pendingGenerationCount() const { return m_pending.size(); }
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
    QSet<QcChunkPos> m_pending;
    QcMailbox<std::shared_ptr<QcChunk>> m_generated;
    // Last member: destroyed first, so no job outlives the generator it uses.
    QcWorkerPool m_generationPool;
};
