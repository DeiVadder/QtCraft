// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcAabb.h"
#include "common/core/QcMailbox.h"
#include "common/core/QcWorkerPool.h"
#include "common/light/QcLightEngine.h"
#include "common/world/QcBlockAccess.h"
#include "common/world/QcChunk.h"
#include "server/blockentities/QcBlockEntities.h"
#include "server/blockentities/QcBlockEntityExtras.h"
#include "server/blockentities/QcSignTexts.h"
#include "server/blocks/QcFireQueue.h"
#include "server/chunks/QcBlockIndex.h"
#include "server/chunks/QcChunkLinks.h"
#include "server/chunks/QcChunkLoaders.h"
#include "server/chunks/QcChunkSaveState.h"
#include "server/chunks/QcSectionMarks.h"
#include "server/machines/QcBeacons.h"
#include "server/machines/QcCampfires.h"
#include "server/spawners/QcSpawners.h"
#include "server/storage/QcChunkIo.h"
#include "server/villages/QcVillages.h"
#include "worldgen/generators/QcVersionedGenerator.h"
#include "worldgen/generators/QcWorldGenerator.h"

#include <QHash>
#include <QSet>

#include <atomic>
#include <functional>
#include <limits>
#include <memory>
#include <vector>
#include <utility>

/*! \brief Authoritative block world of the server.

    Chunks handed out as const snapshots stay immutable: modifying a chunk that is still shared
    copies it first.

    Chunks come into the world generated or read from the world's storage (QcChunkIo, decision
    E-373): requestChunk() starts that in the background, collectArrivals() hands over what is
    ready and admit() puts a chunk into the world, at once (loadChunk() does all three and
    waits). What was kept of a chunk's block entities and entities goes to the arrival handler
    (QcChunkLoading). Chunks leave the world through remove() (QcChunkLoading, decision E-374),
    which hands their blocks back when they changed since they were last kept. */
class QcServerWorld : public QcBlockAccess
{
public:
    /*! A chunk as read or generated, not yet in the world. */
    using Arrival = QcChunkIo::Arrival;
    /*! Takes what was kept of a chunk that enters the world. */
    using ArrivalHandler = std::function<void(Arrival &arrival)>;
    /*! What the storage holds of a chunk. */
    using Stored = QcChunkStore::Stored;

    QcServerWorld(std::shared_ptr<const QcContentRegistry> content, std::unique_ptr<QcWorldGenerator> generator);
    // Its villages ask it which chunks are loaded.
    Q_DISABLE_COPY_MOVE(QcServerWorld)
    ~QcServerWorld() override = default;

    [[nodiscard]] std::optional<QcBlockId> blockAt(QcBlockPos pos) const override;
    const QcContentRegistry &content() const override { return *m_content; }
    bool mayHaveLightSourceNear(QcBlockPos pos, int reach) const override;

    /*! Returns the block and sky light of the loaded chunks (decision E-389), kept in step with
        every block set here and every chunk that enters or leaves the world. */
    const QcLightEngine &light() const { return m_light; }
    /*! Returns the block light at \a pos. */
    int blockLight(QcBlockPos pos) const { return m_light.blockLight(pos); }
    /*! Returns the sky light at \a pos. */
    int skyLight(QcBlockPos pos) const { return m_light.skyLight(pos); }

    /*! Returns the sections of the loaded chunks that may hold blocks with a trait (decision
        E-359): contact damage (QcContactDamage), growing plants, circuit components (block
        changes far from any circuit cost the signal engine nothing), pressure plates, block
        entities. */
    const QcSectionMarks &marks() const { return m_marks; }
    /*! Returns the state that belongs to single blocks (decision E-357), kept in step with every
        block set here, every chunk that arrives and every chunk generated again. */
    QcBlockEntities &blockEntities() { return m_blockEntities; }
    /*! Returns the state that belongs to single blocks, read-only. */
    const QcBlockEntities &blockEntities() const { return m_blockEntities; }
    /*! Returns the loaded Signal Hoppers and the cells changed next to them (decision E-344). */
    QcBlockIndex &hoppers() { return m_hoppers; }
    /*! Returns the loaded Signal Hoppers, read-only. */
    const QcBlockIndex &hoppers() const { return m_hoppers; }
    /*! Returns the loaded daylight sensors (decision E-982): every chunk that arrives or leaves and
        every block set keeps them up to date, so that reading them scans no chunk. */
    const QcBlockIndex &daylightSensors() const { return m_daylightSensors; }
    /*! Returns the burning fires of the loaded chunks and their next steps (decision E-392),
        kept in step with every block set here and every chunk that arrives, leaves or is
        generated again. */
    QcFireQueue &fires() { return m_fires; }
    /*! Returns the burning fires, read-only. */
    const QcFireQueue &fires() const { return m_fires; }
    /*! Returns the monster spawners of the loaded chunks (decision E-520), kept in step like the
        fires. */
    QcSpawners &spawners() { return m_spawners; }
    /*! Returns the monster spawners, read-only. */
    const QcSpawners &spawners() const { return m_spawners; }
    /*! Returns the items cooking on the campfires of the loaded chunks (decision E-772), kept in
        step like the spawners. */
    QcCampfires &campfires() { return m_campfires; }
    /*! Returns the items cooking on the campfires, read-only. */
    const QcCampfires &campfires() const { return m_campfires; }
    /*! Returns the text on the signs of the loaded chunks (decision E-766), kept in step like
        the spawners. */
    QcSignTexts &signs() { return m_signs; }
    /*! Returns the text on the signs, read-only. */
    const QcSignTexts &signs() const { return m_signs; }
    /*! Returns the beacons of the loaded chunks (decision E-1142), kept in step like the signs. */
    QcBeacons &beacons() { return m_beacons; }
    /*! Returns the beacons, read-only. */
    const QcBeacons &beacons() const { return m_beacons; }
    /*! Returns which loaded chunks were furnished and the parts of their saved block entities this
        version does not read (decisions E-520, E-440); kept in step with every block set here. */
    QcBlockEntityExtras &blockEntityExtras() { return m_blockEntityExtras; }
    /*! Returns the furnished chunks and the kept parts of block entities, read-only. */
    const QcBlockEntityExtras &blockEntityExtras() const { return m_blockEntityExtras; }
    /*! Returns the circuits' links across chunk borders (decision E-375). */
    const QcChunkLinks &links() const { return m_links; }
    /*! Returns the chunk loader blocks of the world, loaded or not (decision E-377). */
    QcChunkLoaders &loaders() { return m_loaders; }
    /*! Returns the chunk loader blocks, read-only. */
    const QcChunkLoaders &loaders() const { return m_loaders; }
    /*! Returns the villages of the world, loaded or not (decision E-403). */
    QcVillages &villages() { return *m_villages; }
    /*! Returns the villages, read-only. */
    const QcVillages &villages() const { return *m_villages; }
    /*! Returns the circuit marks of the chunk \a pos: of a loaded one as it is, of another one
        as stored. */
    quint16 linkMarksOf(QcChunkPos pos) const;

    /*! Reads and writes stored chunks through \a io (none: chunks are only generated);
        \a catalog is what the storage holds. The world does not own \a io. */
    void setStorage(QcChunkIo *io, QHash<QcChunkPos, Stored> catalog);
    /*! Returns what the storage holds of the chunks, the loaded chunks that differ from it or
        from the generator, the quarantined chunks (decision E-373). */
    QcChunkSaveState &saves() { return m_saves; }
    /*! Returns what the storage holds of the chunks, read-only. */
    const QcChunkSaveState &saves() const { return m_saves; }
    /*! Sets \a handler, called with what was kept of every chunk that enters the world (read or
        generated). */
    void setArrivalHandler(ArrivalHandler handler) { m_onArrival = std::move(handler); }
    /*! Loads a chunk now with the chunks of its group (QcChunkLoading::loadGroupNow);
        loadChunk() asks it when set, so a chunk loaded at once (spawn, commands) never comes
        without its group. */
    using GroupLoader = std::function<void(QcChunkPos)>;
    /*! Sets \a loader as the group loader. */
    void setGroupLoader(GroupLoader loader) { m_loadGroup = std::move(loader); }
    /*! Returns the chunk \a pos as read or generated now, not yet in the world (for admit()); a
        background request of it is superseded. */
    Arrival produceNow(QcChunkPos pos);

    /*! Returns the loaded chunk \a pos; null when it is not loaded. */
    std::shared_ptr<const QcChunk> chunk(QcChunkPos pos) const;
    /*! Returns the chunk \a pos, reading or generating it synchronously when needed (spawn,
        commands, tests). */
    std::shared_ptr<const QcChunk> loadChunk(QcChunkPos pos);
    /*! Starts reading or generating the chunk \a pos in the background unless it is loaded or
        under way. */
    void requestChunk(QcChunkPos pos);
    /*! Returns whether the chunk \a pos is being read or generated in the background. */
    bool isRequested(QcChunkPos pos) const { return m_pending.contains(pos); }
    /*! Returns the chunks read or generated in the background since the last call; not yet in
        the world (admit). */
    std::vector<Arrival> collectArrivals();
    /*! Puts \a arrival, an arrived chunk, into the world and passes what was kept of it to the
        arrival handler. */
    void admit(Arrival &&arrival);
    /*! Moves every finished background chunk into the world at once (worlds without groups:
        tests). Returns their positions. */
    QList<QcChunkPos> collectGeneratedChunks();
    /*! Returns the chunks being read or generated in the background. */
    qsizetype pendingGenerationCount() const { return m_pending.size(); }
    /*! Plans the generator's guardian zones and spawn areas within \a reach of \a from on a
        generation thread, so the tick thread finds them planned
        (QcWorldGenerator::plannedStructureSitesNear, QcGuardians, QcSpawnAreas); one such job at
        a time, a call while one runs is dropped. */
    void planStructureSitesInBackground(QcBlockPos from, int reach);

    /*! Takes the chunk \a pos out of the world with what was derived from its blocks (marks,
        hoppers, links); its block entities must have been taken before
        (QcBlockEntityRecord::takeFrom). Returns its blocks if they changed since they were last
        kept. */
    std::shared_ptr<const QcChunk> remove(QcChunkPos pos);
    /*! Removes the loaded chunk \a pos whatever its state and returns it (tests of circuits at
        the edge of the loaded area; nothing is kept). */
    std::shared_ptr<QcChunk> takeChunk(QcChunkPos pos);
    /*! Drops the chunk \a pos, if unchanged, without keeping anything (worlds without storage:
        tests); changed ones stay. Empty, resting containers and idle machines go; they come back
        with their blocks. */
    void unloadChunk(QcChunkPos pos);
    /*! Returns the number of loaded chunks. */
    qsizetype loadedChunkCount() const { return m_chunks.size(); }
    /*! Returns a count that changes whenever a chunk enters or leaves the world
        (QcEntities::settle). */
    quint64 loadedEpoch() const { return m_loadedEpoch; }
    /*! Returns the count of block changes of the world (decision E-920): every changed block and
        every chunk that entered the world or was generated again. */
    quint64 blockChanges() const { return m_blockChanges; }
    /*! Returns the count of blockChanges() when a block of the chunk \a pos last changed or the
        chunk entered the world; the largest count there is for a chunk that is not loaded. A
        resting boat compares it with the count when it came to rest instead of reading its
        blocks every tick. */
    quint64 lastChangeIn(QcChunkPos pos) const { return m_lastChange.value(pos, std::numeric_limits<quint64>::max()); }
    /*! Returns the chunks that became loaded (generated, loaded synchronously or read from a
        save) since the last call. */
    QList<QcChunkPos> takeNewlyLoaded() { return std::exchange(m_newlyLoaded, {}); }
    /*! Returns the loaded chunks. */
    QList<QcChunkPos> loadedChunks() const { return m_chunks.keys(); }

    /*! Replaces the loaded chunk \a pos with what the generator makes of it (decision E-246);
        its block entities are forgotten and those of the generated blocks created. It counts as
        changed, so the next save also overwrites the stored copy. Returns \c false when it is
        not loaded. */
    bool regenerateChunk(QcChunkPos pos);

    /*! Sets the block at \a pos to \a block. Returns \c false for unloaded chunks, positions
        outside the world height and unknown block ids. The block entities follow
        (QcBlockEntities::placed); spilled contents wait in blockEntities(). */
    bool setBlock(QcBlockPos pos, QcBlockId block);

    /*! Returns the world generator: the one the world was made with, following the version map
        of its dimension (QcVersionedGenerator, decision E-1012). */
    const QcWorldGenerator &generator() const { return *m_generator; }
    /*! Returns whether the chunk \a pos has an entry in the generator's version map. */
    [[nodiscard]] bool isVersionMapped(QcChunkPos pos) const { return m_generator->versionMap().versionAt(pos).has_value(); }
    /*! Generates the chunks \a map names with generators of their versions made by \a create
        (QcVersionedGenerator::setVersionMap): only before the first chunk is generated. Returns
        an error then or for a version \a create cannot make. */
    [[nodiscard]] QcStatus setGeneratorVersionMap(const QcGeneratorVersionMap &map, const QcVersionedGenerator::Factory &create)
    {
        return m_generator->setVersionMap(map, create);
    }

    /*! Puts \a chunks into the world as changed (tests). */
    void adoptStoredChunks(const QHash<QcChunkPos, std::shared_ptr<QcChunk>> &chunks);
    /*! Returns the blocks of the changed chunk \a pos as a snapshot for keeping (a save); it
        counts as unchanged then. */
    std::shared_ptr<const QcChunk> takeChanged(QcChunkPos pos);

private:
    // Records the marked sections, the hoppers, the links, the block entities and the fires of a chunk
    // that entered the world.
    void noteSections(QcChunkPos pos, const QcChunk &chunk);
    // A block of the chunk changed or the chunk entered the world (lastChangeIn).
    void noteChange(QcChunkPos pos);
    // Forgets what was derived from a chunk that left the world or is generated again.
    void forgetSections(QcChunkPos pos);
    // `light`: the chunk's own light prepared in the background (null: computed here).
    void insert(QcChunkPos pos, const std::shared_ptr<QcChunk> &chunk, const std::shared_ptr<QcLightEngine::Prepared> &light);
    void generateInBackground(QcChunkPos pos, Arrival kept);

    std::shared_ptr<const QcContentRegistry> m_content;
    std::unique_ptr<QcVersionedGenerator> m_generator;
    QcSectionMarks m_marks;
    QcBlockIndex m_hoppers;
    // NOLINT(qc-member-access): the sensors' index, built in the constructor, kept by noteSections, forgetSections and setBlock, read only through daylightSensors().
    QcBlockIndex m_daylightSensors;
    QcChunkLinks m_links;
    QcChunkLoaders m_loaders;
    // Refers to the generator, which lives as long as the world.
    std::unique_ptr<QcVillages> m_villages;
    QcBlockEntities m_blockEntities;
    QcFireQueue m_fires;
    QcSpawners m_spawners;
    QcCampfires m_campfires;
    QcSignTexts m_signs;
    QcBeacons m_beacons;
    QcBlockEntityExtras m_blockEntityExtras;
    QHash<QcChunkPos, std::shared_ptr<QcChunk>> m_chunks;
    // Reads the chunks of m_chunks in place (told about every replaced copy).
    QcLightEngine m_light;
    // Chunks under way, with the number of their latest request.
    QHash<QcChunkPos, quint64> m_pending;
    quint64 m_nextRequest = 1;
    QcChunkSaveState m_saves;
    QList<QcChunkPos> m_newlyLoaded;
    quint64 m_loadedEpoch = 0;
    quint64 m_blockChanges = 0;
    // NOLINT(qc-member-access): the change clock's table, set only by noteChange, dropped for a chunk that leaves (forgetSections), read only by lastChangeIn.
    QHash<QcChunkPos, quint64> m_lastChange;
    ArrivalHandler m_onArrival;
    GroupLoader m_loadGroup;
    QcMailbox<Arrival> m_generated;
    std::shared_ptr<std::atomic<bool>> m_planningSites = std::make_shared<std::atomic<bool>>(false);
    // Last member: destroyed first, so no job outlives the generator it uses.
    QcWorkerPool m_generationPool;
};
