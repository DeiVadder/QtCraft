#pragma once

#include "QcAabb.h"
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

    // The strongest contact damage (lava) of the loaded blocks a box overlaps, 0 for none.
    double contactDamageIn(const QcAabb &box) const;
    // False when no loaded chunk holds a block with contact damage, so nothing can burn.
    bool mayHaveContactDamage() const { return !m_damagingSections.isEmpty(); }
    // Sections of the loaded chunks that may hold a block with random ticks (growing plants, crops, soil).
    const QcSectionMarks &growingSections() const { return m_growingSections; }
    // Sections that may hold a Signal Dust component (decision E-335): block changes far from any
    // circuit cost the signal engine nothing.
    const QcSectionMarks &signalSections() const { return m_signalSections; }
    // Sections that may hold a pressure plate: only bodies there are checked for pressing one.
    const QcSectionMarks &plateSections() const { return m_plateSections; }
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
    // Modified chunks stay loaded until the world closes so that saves always see them.
    void unloadChunk(QcChunkPos pos);
    qsizetype loadedChunkCount() const { return m_chunks.size(); }
    // Chunks that became loaded (generated, loaded synchronously or read from a save) since the last call.
    QList<QcChunkPos> takeNewlyLoaded() { return std::exchange(m_newlyLoaded, {}); }
    QList<QcChunkPos> loadedChunks() const { return m_chunks.keys(); }

    // Replaces a loaded chunk with what the generator makes of it (decision E-246). It stays marked
    // as modified, so the next save also overwrites the stored copy. False when it is not loaded
    // (then nothing was changed in it since it was last generated).
    bool regenerateChunk(QcChunkPos pos);

    // Fails for unloaded chunks, positions outside the world height and unknown block ids.
    bool setBlock(QcBlockPos pos, QcBlockId block);

    // Block light (0..15) at a position, computed from the light sources around it in loaded chunks
    // (decision E-049: block light is derived, never stored). Cheap when no source is near.
    int blockLightAt(QcBlockPos pos) const;

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
    // Records which sections of a chunk that entered the world hold blocks with contact damage,
    // growing plants or circuit components.
    void noteSections(QcChunkPos pos, const QcChunk &chunk);

    std::shared_ptr<const QcContentRegistry> m_content;
    std::unique_ptr<QcWorldGenerator> m_generator;
    // Contact damage of every block state, indexed by id.
    std::vector<double> m_contactDamage;
    QcSectionMarks m_damagingSections;
    QcSectionMarks m_growingSections;
    QcSectionMarks m_signalSections;
    QcSectionMarks m_plateSections;
    QcBlockIndex m_hoppers;
    QHash<QcChunkPos, std::shared_ptr<QcChunk>> m_chunks;
    QSet<QcChunkPos> m_pending;
    QSet<QcChunkPos> m_modified;
    QSet<QcChunkPos> m_dirtyRegions;
    QList<QcChunkPos> m_newlyLoaded;
    QcMailbox<std::shared_ptr<QcChunk>> m_generated;
    // Last member: destroyed first, so no job outlives the generator it uses.
    QcWorkerPool m_generationPool;
};
