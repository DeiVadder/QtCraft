#pragma once

#include "QcAabb.h"
#include "QcBlockEntities.h"
#include "QcBlockIndex.h"
#include "QcBlockAccess.h"
#include "QcChunk.h"
#include "QcChunkIo.h"
#include "QcChunkLinks.h"
#include "QcChunkLoaders.h"
#include "QcContentRegistry.h"
#include "QcLightEngine.h"
#include "QcMailbox.h"
#include "QcSectionMarks.h"
#include "QcWorkerPool.h"
#include "QcWorldGenerator.h"

#include <QHash>
#include <QSet>

#include <functional>
#include <memory>
#include <vector>
#include <utility>

// Authoritative block world of the server. Chunks handed out as const snapshots stay immutable:
// modifying a chunk that is still shared copies it first.
//
// Chunks come into the world generated or read from the world's storage (QcChunkIo, decision
// E-373): requestChunk() starts that in the background, collectArrivals() hands over what is ready
// and admit() puts a chunk into the world, at once (loadChunk() does all three and waits). What was
// kept of a chunk's block entities and entities goes to the arrival handler (QcChunkLoading). Chunks
// leave the world through remove() (QcChunkLoading, decision E-374), which hands their blocks back
// when they changed since they were last kept.
class QcServerWorld : public QcBlockAccess
{
public:
    using Arrival = QcChunkIo::Arrival;
    using ArrivalHandler = std::function<void(Arrival &arrival)>;
    using Stored = QcChunkStore::Stored;

    QcServerWorld(std::shared_ptr<const QcContentRegistry> content, std::unique_ptr<QcWorldGenerator> generator);

    [[nodiscard]] std::optional<QcBlockId> blockAt(QcBlockPos pos) const override;
    const QcContentRegistry &content() const override { return *m_content; }
    bool mayHaveLightSourceNear(QcBlockPos pos, int reach) const override;

    // Block and sky light of the loaded chunks (decision E-389), kept in step with every block set
    // here and every chunk that enters or leaves the world.
    const QcLightEngine &light() const { return m_light; }
    int blockLight(QcBlockPos pos) const { return m_light.blockLight(pos); }
    int skyLight(QcBlockPos pos) const { return m_light.skyLight(pos); }

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
    // Circuits across chunk borders (decision E-375).
    const QcChunkLinks &links() const { return m_links; }
    // Chunk loader blocks of the world, loaded or not (decision E-377).
    QcChunkLoaders &loaders() { return m_loaders; }
    const QcChunkLoaders &loaders() const { return m_loaders; }
    // The circuit marks of a chunk: of a loaded one as it is, of another one as stored.
    quint16 linkMarksOf(QcChunkPos pos) const;

    // Reads and writes stored chunks through `io` (none: chunks are only generated); `catalog` is what
    // the storage holds. The world does not own `io`.
    void setStorage(QcChunkIo *io, QHash<QcChunkPos, Stored> catalog);
    QcChunkIo *storage() const { return m_io; }
    // What the storage holds of a chunk, as far as the server knows (kept up to date by noteKept()).
    [[nodiscard]] std::optional<Stored> stored(QcChunkPos pos) const;
    qsizetype storedChunkCount() const { return m_catalog.size(); }
    QList<QcChunkPos> storedChunks() const { return m_catalog.keys(); }
    // Called with what was kept of every chunk that enters the world (read or generated).
    void setArrivalHandler(ArrivalHandler handler) { m_onArrival = std::move(handler); }
    // Loads a chunk now with the chunks of its group (QcChunkLoading::loadGroupNow); loadChunk() asks
    // it when set, so a chunk loaded at once (spawn, commands) never comes without its group.
    using GroupLoader = std::function<void(QcChunkPos)>;
    void setGroupLoader(GroupLoader loader) { m_loadGroup = std::move(loader); }
    // A chunk as read or generated now, not yet in the world (for admit()); a background request of
    // it is superseded.
    Arrival produceNow(QcChunkPos pos);

    std::shared_ptr<const QcChunk> chunk(QcChunkPos pos) const;
    // Returns the chunk, reading or generating it synchronously when needed (spawn, commands, tests).
    std::shared_ptr<const QcChunk> loadChunk(QcChunkPos pos);
    // Starts reading or generating a chunk in the background unless it is loaded or under way.
    void requestChunk(QcChunkPos pos);
    bool isRequested(QcChunkPos pos) const { return m_pending.contains(pos); }
    // Chunks read or generated in the background since the last call; not yet in the world (admit).
    std::vector<Arrival> collectArrivals();
    // Puts an arrived chunk into the world and passes what was kept of it to the arrival handler.
    void admit(Arrival &&arrival);
    // Moves every finished background chunk into the world at once (worlds without groups: tests).
    QList<QcChunkPos> collectGeneratedChunks();
    qsizetype pendingGenerationCount() const { return m_pending.size(); }
    // Chunks whose stored data is damaged (decision E-373): they play as generated and are never
    // saved, until /regenerate replaces their stored data (regenerateChunk).
    bool isQuarantined(QcChunkPos pos) const { return m_quarantined.contains(pos); }
    // Chunks found damaged since the last call (the server tells the operators).
    QList<QcChunkPos> takeNewlyQuarantined() { return std::exchange(m_newlyQuarantined, {}); }

    // Takes a chunk out of the world with what was derived from its blocks (marks, hoppers, links);
    // its block entities must have been taken before (QcBlockEntityRecords::take). Returns its blocks
    // if they changed since they were last kept.
    std::shared_ptr<const QcChunk> remove(QcChunkPos pos);
    // Removes a loaded chunk whatever its state and returns it (tests of circuits at the edge of the
    // loaded area; nothing is kept).
    std::shared_ptr<QcChunk> takeChunk(QcChunkPos pos);
    // Drops an unchanged chunk without keeping anything (worlds without storage: tests); changed ones
    // stay. Empty, resting containers and idle machines go; they come back with their blocks.
    void unloadChunk(QcChunkPos pos);
    qsizetype loadedChunkCount() const { return m_chunks.size(); }
    // Changes whenever a chunk enters or leaves the world (QcEntities::settle).
    quint64 loadedEpoch() const { return m_loadedEpoch; }
    // Chunks that became loaded (generated, loaded synchronously or read from a save) since the last call.
    QList<QcChunkPos> takeNewlyLoaded() { return std::exchange(m_newlyLoaded, {}); }
    QList<QcChunkPos> loadedChunks() const { return m_chunks.keys(); }

    // Replaces a loaded chunk with what the generator makes of it (decision E-246); its block
    // entities are forgotten and those of the generated blocks created. It counts as changed, so the
    // next save also overwrites the stored copy. False when it is not loaded.
    bool regenerateChunk(QcChunkPos pos);

    // Fails for unloaded chunks, positions outside the world height and unknown block ids. The block
    // entities follow (QcBlockEntities::placed); spilled contents wait in blockEntities().
    bool setBlock(QcBlockPos pos, QcBlockId block);

    const QcWorldGenerator &generator() const { return *m_generator; }

    // Chunks put into the world as changed (tests).
    void adoptStoredChunks(const QHash<QcChunkPos, std::shared_ptr<QcChunk>> &chunks);
    // Whether the blocks of a loaded chunk changed since they were last kept.
    bool isChanged(QcChunkPos pos) const { return m_changed.contains(pos); }
    // Whether the blocks of a loaded chunk differ from what the generator makes (changed while
    // loaded or read from storage).
    bool differsFromGenerated(QcChunkPos pos) const { return m_differs.contains(pos); }
    qsizetype changedChunkCount() const { return m_changed.size(); }
    // The blocks of a changed chunk as a snapshot for keeping (a save); it counts as unchanged then.
    std::shared_ptr<const QcChunk> takeChanged(QcChunkPos pos);
    // The chunk counts as changed again (a save failed).
    void markChanged(QcChunkPos pos);
    // What was handed to the storage for a chunk (keeps stored() up to date).
    void noteKept(const QcChunkIo::Departure &departure);

private:
    // Records the marked sections, the hoppers, the links and the block entities of a chunk that
    // entered the world.
    void noteSections(QcChunkPos pos, const QcChunk &chunk);
    // Forgets what was derived from a chunk that left the world or is generated again.
    void forgetSections(QcChunkPos pos);
    // `light`: the chunk's own light prepared in the background (null: computed here).
    void insert(QcChunkPos pos, const std::shared_ptr<QcChunk> &chunk, const std::shared_ptr<QcLightEngine::Prepared> &light);
    void generateInBackground(QcChunkPos pos, Arrival kept);

    std::shared_ptr<const QcContentRegistry> m_content;
    std::unique_ptr<QcWorldGenerator> m_generator;
    QcSectionMarks m_marks;
    QcBlockIndex m_hoppers;
    QcChunkLinks m_links;
    QcChunkLoaders m_loaders;
    QcBlockEntities m_blockEntities;
    QHash<QcChunkPos, std::shared_ptr<QcChunk>> m_chunks;
    // Reads the chunks of m_chunks in place (told about every replaced copy).
    QcLightEngine m_light;
    // Chunks under way, with the number of their latest request.
    QHash<QcChunkPos, quint64> m_pending;
    quint64 m_nextRequest = 1;
    QSet<QcChunkPos> m_changed;
    QSet<QcChunkPos> m_differs;
    QSet<QcChunkPos> m_quarantined;
    QList<QcChunkPos> m_newlyQuarantined;
    QList<QcChunkPos> m_newlyLoaded;
    quint64 m_loadedEpoch = 0;
    QcChunkIo *m_io = nullptr;
    QHash<QcChunkPos, Stored> m_catalog;
    ArrivalHandler m_onArrival;
    GroupLoader m_loadGroup;
    QcMailbox<Arrival> m_generated;
    // Last member: destroyed first, so no job outlives the generator it uses.
    QcWorkerPool m_generationPool;
};
