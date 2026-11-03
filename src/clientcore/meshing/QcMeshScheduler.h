// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/light/QcBlockLightEngine.h"
#include "clientcore/light/QcChunkLight.h"
#include "clientcore/meshing/QcBlockRenderTable.h"
#include "clientcore/meshing/QcChunkMesh.h"
#include "clientcore/meshing/QcChunkMesher.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcMailbox.h"
#include "common/core/QcSectionKey.h"
#include "common/core/QcVec3.h"
#include "common/core/QcWorkerPool.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSet>

#include <functional>
#include <memory>
#include <optional>
#include <utility>

class QcClientWorld;

/*! \brief Keeps section meshes in sync with the client world.

    Dirty sections are meshed on private worker threads from immutable chunk snapshots, nearest
    to the camera first; results come back to the owning thread as signals. A section is only
    meshed once its four side neighbours are loaded and the block light of its chunk is known.
    Block light is updated incrementally by a QcBlockLightEngine on a thread of its own; only
    sections whose light changed are meshed again. The local player's own block changes go first
    (decision E-853). */
class QcMeshScheduler : public QObject
{
    Q_OBJECT

public:
    /*! \brief Who changed a block, and so how soon it is meshed. */
    enum class Priority
    {
        /*! The server's changes and the streaming: in the order of pump(), nearest first. */
        Normal,
        /*! The local player's predicted change or its rollback: the sections holding its faces
            are meshed when the event loop comes back (each section once for all changes of that
            pass, e.g. both halves of a door), before every waiting job and beyond
            maxJobsInFlight, with the block light known so far; a change of the block light
            follows with the light job. */
        Player,
    };

    /*! \brief How quickly block changes reach the screen (benchmark): time from a change until
        every section it made dirty has a new mesh, the largest number of sections and light jobs
        waiting, and the time the light thread spent. */
    struct Stats
    {
        /*! Block changes whose sections all got their new mesh. */
        quint64 latencySamples = 0;
        /*! Sum of their latencies in milliseconds. */
        double latencyTotalMs = 0.0;
        /*! Largest latency in milliseconds. */
        double latencyWorstMs = 0.0;
        /*! Largest number of dirty sections, mesh jobs and light jobs waiting at once. */
        qsizetype worstBacklog = 0;
        /*! Light jobs finished. */
        quint64 lightJobs = 0;
        /*! Light thread time in milliseconds, in total. */
        double lightTotalMs = 0.0;
        /*! Light thread time of the longest job in milliseconds. */
        double lightWorstMs = 0.0;
        /*! Mesh and light jobs dropped unstarted by clear(). */
        qsizetype discardedJobs = 0;
        /*! Mesh results that arrived, outdated ones included. */
        quint64 meshJobs = 0;
        /*! Sum of the mesh workers' time in milliseconds. */
        double meshTotalMs = 0.0;
        /*! Longest mesh job in milliseconds. */
        double meshWorstMs = 0.0;
    };

    explicit QcMeshScheduler(QObject *parent = nullptr);
    ~QcMeshScheduler() override;
    Q_DISABLE_COPY_MOVE(QcMeshScheduler)

    /*! Sets the render data of the block states \a table and starts a new light engine for it; the
        block light of all chunks is computed again. */
    void setRenderTable(std::shared_ptr<const QcBlockRenderTable> table);
    /*! Sets the camera \a position: dirty sections nearest to it are meshed first. */
    void setFocus(const QcVec3 &position) { m_focus = position; }
    /*! Sets whether meshes carry the light of their cells (\a on; QcMeshInput::sectionLight);
        for the sections meshed from then on. */
    void setSectionLight(bool on) { m_sectionLight = on; }

    /*! Notes that the chunk at \a pos of \a world is loaded: its sections and those of its
        neighbours are meshed once its block light is known. */
    void chunkLoaded(const QcClientWorld &world, QcChunkPos pos);
    /*! Forgets the chunk at \a pos: drops its dirty sections and light and emits meshRemoved() for
        each of its sections. */
    void chunkUnloaded(QcChunkPos pos);
    /*! The block at \a pos changed from \a previous to what \a world holds now, with
        \a priority. Light changes of the block changes of one pass of the event loop go to the
        light engine together, as one job (an explosion's hundreds of blocks, decision E-604). A
        change between two states drawn alike (equal render table entries) meshes nothing. */
    void blockChanged(const QcClientWorld &world, QcBlockPos pos, QcBlockId previous, Priority priority = Priority::Normal);
    /*! Forgets every chunk, dirty section and light and drops the waiting jobs (another world). */
    void clear();
    /*! Marks every section of the loaded chunks dirty, so each is meshed and delivered again
        (the renderer lost its GPU copies, QcWorldView::meshesLost). */
    void remeshAll();

    /*! Starts jobs for dirty sections of \a world, at most \c {maxJobsInFlight} at a time. */
    void pump(const QcClientWorld &world);

    /*! Returns the number of sections waiting for a mesh job. */
    qsizetype dirtyCount() const { return m_dirty.size(); }
    /*! Returns the number of mesh jobs running or queued. */
    qsizetype jobsInFlight() const { return m_inFlight.size(); }
    /*! Returns whether the loaded chunk at \a pos has its light and no section waiting for or in
        a mesh job (the loading screen, decision E-943). A section whose side neighbour is
        missing is not marked, so the caller checks the neighbours. */
    [[nodiscard]] bool chunkMeshed(QcChunkPos pos) const;
    /*! Returns the light jobs submitted or gathered and not yet back. */
    int lightJobsPending() const { return m_lightJobsPending + (m_gathered.isEmpty() ? 0 : 1); }
    /*! Returns the bytes of the block light sections handed to the mesher (memory statistics,
        decision E-813); the light engine on its own thread holds about as many again. Walks
        every section: for occasional statistics, not per frame. */
    qint64 lightBytes() const;
    /*! Returns the block light (0..15) at \a pos as the meshes show it; 0 where it is not known
        yet. */
    int blockLight(QcBlockPos pos) const;
    /*! Submits the gathered light changes and blocks until all queued jobs finished; their
        results are delivered through the event loop. */
    void waitForJobs();

    /*! Returns the statistics since the last resetStats(). */
    Stats stats() const { return m_stats; }
    /*! Starts the statistics over. */
    void resetStats() { m_stats = {}; }

signals:
    /*! Emitted when the section \a key (qcSectionKey()) has a new \a mesh. */
    void meshReady(quint64 key, std::shared_ptr<const QcChunkMesh> mesh);
    /*! Emitted when the section \a key is gone (its chunk unloaded). */
    void meshRemoved(quint64 key);

private:
    struct Result
    {
        quint64 key = 0;
        quint64 version = 0;
        std::shared_ptr<const QcChunkMesh> mesh;
        double workMs = 0.0;
    };

    struct LightResult
    {
        quint64 generation = 0;
        // Set for the job that added a chunk: its complete light.
        std::optional<QcChunkPos> addedChunk;
        std::shared_ptr<const QcChunkLight> addedLight;
        QcBlockLightEngine::Changes changes;
        // Sections the change itself made dirty, meshed together with the new light.
        QList<QcSectionAddress> dirty;
        // Start of the change in m_clock time, or -1.
        qint64 changedAtNs = -1;
        double workMs = 0.0;
    };

    void markDirty(QcChunkPos chunk, int section, qint64 changedAtNs = -1);
    // Sections whose faces or ambient occlusion a change of the block at `pos` affects.
    static QList<QcSectionAddress> faceSections(QcBlockPos pos);
    // Sky light travels up to 15 blocks sideways, so it reaches the 3 x 3 chunk neighbourhood. A
    // section's sky light (QcChunkMesher) depends on the cells from 16 blocks below to 16 blocks
    // above it and on where each column's highest light-dimming block lies: it changes when that
    // range holds the changed cell or a cell that turns lit or shadowed because the column's highest
    // dimming block moved. Returns the lowest and highest such cell for a change at `pos`.
    std::pair<int, int> skyLightRange(const QcClientWorld &world, QcBlockPos pos, const QcBlockRenderTable::Entry &before,
                                      const QcBlockRenderTable::Entry &after) const;
    // Dirty sections not being meshed, nearest to the focus first.
    QList<quint64> nearestDirtyFirst() const;
    // Nullopt while a side neighbour of the chunk is missing. A neighbour beyond the bound of the world
    // (QcChunkPos::isInsideWorld) never arrives and stays null; QcChunkMesher treats it as an opaque wall.
    [[nodiscard]] std::optional<QcMeshInput> meshInput(const QcClientWorld &world, QcSectionAddress address) const;
    void submitMesh(quint64 key, QcMeshInput input, Priority priority);
    // Starts the job of the dirty section `key`, or drops the mark while it cannot be meshed yet
    // (a later mark brings it back) or emits the removal of an empty section.
    void startJob(const QcClientWorld &world, quint64 key, Priority priority);
    // Marks the sections dirty and gathers them for submitPlayerSections() (Priority::Player).
    void gatherPlayerSections(const QcClientWorld &world, const QList<QcSectionAddress> &sections, qint64 changedAtNs);
    // Starts the jobs of the gathered player sections that are still dirty, each once.
    void submitPlayerSections();
    // Counts a job of `key` in or out of m_inFlight.
    void jobStarted(quint64 key);
    void jobFinished(quint64 key);
    // Records the time since the section's earliest unshown change, if any.
    void noteLatency(quint64 key);
    // The chunk and its loaded neighbours, all sections.
    void markChunkAndNeighbours(QcChunkPos pos);
    void submitLight(std::function<void(QcBlockLightEngine &)> work, std::optional<QcChunkPos> added, QList<QcSectionAddress> dirty,
                     qint64 changedAtNs);
    void deliverResults();
    void deliverLight();
    // Submits the gathered light changes of block changes as one job.
    void submitGathered();
    void setLight(QcChunkPos chunk, int section, std::shared_ptr<const QcChunkLight::Section> light);
    void noteBacklog();

    std::shared_ptr<const QcBlockRenderTable> m_renderTable;
    QHash<quint64, QcSectionAddress> m_dirty;
    // Version of the latest dirty mark per section; results of older jobs are discarded.
    QHash<quint64, quint64> m_versions;
    quint64 m_lastVersion = 0;
    // Jobs per section being meshed: a player change may start a newer job while an older one runs.
    QHash<quint64, int> m_inFlight;
    QSet<QcChunkPos> m_loadedChunks;
    QcVec3 m_focus;
    int m_maxJobsInFlight = 1;
    bool m_sectionLight = false;
    QcMailbox<Result> m_results;
    // Block light per chunk as published by the engine; present once the chunk's light is known.
    QHash<QcChunkPos, std::shared_ptr<const QcChunkLight>> m_light;
    std::shared_ptr<QcBlockLightEngine> m_lightEngine;
    // Results of an engine replaced by setRenderTable() or clear() are dropped.
    quint64 m_lightGeneration = 0;
    int m_lightJobsPending = 0;
    // Light changes of block changes not yet submitted (submitGathered), with the dirty sections they
    // bring and the time of the first.
    QList<std::pair<QcBlockPos, std::shared_ptr<const QcChunk>>> m_gathered;
    QList<QcSectionAddress> m_gatheredDirty;
    qint64 m_gatheredSinceNs = -1;
    // Sections of the player's own changes not yet submitted, and the world they are meshed from;
    // cleared by clear().
    QList<quint64> m_playerSections;
    const QcClientWorld *m_playerWorld = nullptr;
    QcMailbox<LightResult> m_lightResults;
    // Earliest unshown block change per dirty section (latency statistics).
    QHash<quint64, qint64> m_changedAt;
    QElapsedTimer m_clock;
    Stats m_stats;
    // Declared last so they are destroyed first: workers must stop before the members they use.
    QcWorkerPool m_lightPool{1};
    QcWorkerPool m_pool;
};
