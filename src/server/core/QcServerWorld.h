#pragma once

#include "QcAabb.h"
#include "QcBlockEntities.h"
#include "QcBlockIndex.h"
#include "QcBlockAccess.h"
#include "QcChunk.h"
#include "QcContentRegistry.h"
#include "QcMailbox.h"
#include "QcSectionMarks.h"
#include "QcWorkerPool.h"
#include "QcWorldGenerator.h"

#include <QHash>
#include <QSet>

#include <memory>
#include <vector>
#include <utility>

// Authoritative block world of the server. Chunks handed out as const snapshots stay immutable:
// modifying a chunk that is still shared copies it first.
class QcServerWorld : public QcBlockAccess
{
public:
    QcServerWorld(std::shared_ptr<const QcContentRegistry> content, std::unique_ptr<QcWorldGenerator> generator);

    std::optional<QcBlockId> blockAt(QcBlockPos pos) const override;
    const QcContentRegistry &content() const override { return *m_content; }
    bool mayHaveLightSourceNear(QcBlockPos pos, int reach) const override;

    // Sections of the loaded chunks that may hold blocks with a trait (decision E-359): contact
    // damage (QcContactDamage), growing plants, circuit components (block changes far from any
    // circuit cost the signal engine nothing), pressure plates, block entities.
    const QcSectionMarks &marks() const { return m_marks; }
    // State that belongs to single blocks (decision E-357), kept in step with every block set here,
    // every chunk that arrives and every chunk generated again.
    QcBlockEntities &blockEntities() { return m_blockEntities; }
    const QcBlockEntities &blockEntities() const { return m_blockEntities; }
    // The loaded Signal Hoppers and the cells changed next to them (decision E-344).
    QcBlockIndex &hoppers() { return m_hoppers; }
    const QcBlockIndex &hoppers() const { return m_hoppers; }
    // Removes a loaded chunk whatever its state and returns it (tests of circuits at the edge of the
    // loaded area; nothing is saved).
    std::shared_ptr<QcChunk> takeChunk(QcChunkPos pos);

    std::shared_ptr<const QcChunk> chunk(QcChunkPos pos) const;
    // Returns the chunk, generating it synchronously when needed (spawn, tests).
    std::shared_ptr<const QcChunk> loadChunk(QcChunkPos pos);
    // Queues background generation unless the chunk is loaded or already queued.
    void requestChunk(QcChunkPos pos);
    // Moves finished background chunks into the world; returns their positions.
    QList<QcChunkPos> collectGeneratedChunks();
    qsizetype pendingGenerationCount() const { return m_pending.size(); }
    // Modified chunks stay loaded until the world closes so that saves always see them. Empty,
    // resting containers and idle machines of an unloaded chunk are forgotten; they come back with
    // their blocks.
    void unloadChunk(QcChunkPos pos);
    qsizetype loadedChunkCount() const { return m_chunks.size(); }
    // Chunks that became loaded (generated, loaded synchronously or read from a save) since the last call.
    QList<QcChunkPos> takeNewlyLoaded() { return std::exchange(m_newlyLoaded, {}); }
    QList<QcChunkPos> loadedChunks() const { return m_chunks.keys(); }

    // Replaces a loaded chunk with what the generator makes of it (decision E-246); its block
    // entities are forgotten and those of the generated blocks created. It stays marked as
    // modified, so the next save also overwrites the stored copy. False when it is not loaded (then
    // nothing was changed in it since it was last generated).
    bool regenerateChunk(QcChunkPos pos);

    // Fails for unloaded chunks, positions outside the world height and unknown block ids. The block
    // entities follow (QcBlockEntities::placed); spilled contents wait in blockEntities().
    bool setBlock(QcBlockPos pos, QcBlockId block);

    const QcWorldGenerator &generator() const { return *m_generator; }

    // Chunks read from a save; they count as modified.
    void adoptStoredChunks(const QHash<QcChunkPos, std::shared_ptr<QcChunk>> &chunks);
    bool isModified(QcChunkPos pos) const { return m_modified.contains(pos); }
    qsizetype modifiedChunkCount() const { return m_modified.size(); }
    // All modified chunks of regions changed since the last call, as snapshots for saving.
    QHash<QcChunkPos, std::shared_ptr<const QcChunk>> takeChunksToSave();
    // Marks the regions of these chunks for the next save again (after a failed write).
    void markDirty(const QList<QcChunkPos> &chunks);

private:
    // Records the marked sections, the hoppers and the block entities of a chunk that entered the world.
    void noteSections(QcChunkPos pos, const QcChunk &chunk);
    // Forgets what was derived from a chunk that left the world or is generated again.
    void forgetSections(QcChunkPos pos);

    std::shared_ptr<const QcContentRegistry> m_content;
    std::unique_ptr<QcWorldGenerator> m_generator;
    QcSectionMarks m_marks;
    QcBlockIndex m_hoppers;
    QcBlockEntities m_blockEntities;
    QHash<QcChunkPos, std::shared_ptr<QcChunk>> m_chunks;
    QSet<QcChunkPos> m_pending;
    QSet<QcChunkPos> m_modified;
    QSet<QcChunkPos> m_dirtyRegions;
    QList<QcChunkPos> m_newlyLoaded;
    QcMailbox<std::shared_ptr<QcChunk>> m_generated;
    // Last member: destroyed first, so no job outlives the generator it uses.
    QcWorkerPool m_generationPool;
};
