#pragma once

#include "clientcore/light/QcBlockLightEngine.h"
#include "clientcore/light/QcChunkLight.h"
#include "clientcore/meshing/QcBlockRenderTable.h"
#include "clientcore/meshing/QcChunkMesh.h"
#include "clientcore/meshing/QcChunkMesher.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcMailbox.h"
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

// Keeps section meshes in sync with the client world. Dirty sections are meshed on private
// worker threads from immutable chunk snapshots, nearest to the camera first; results come back to the
// owning thread as signals. A section is only meshed once its four side neighbours are loaded and
// the block light of its chunk is known. Block light is updated incrementally by a
// QcBlockLightEngine on a thread of its own; only sections whose light changed are meshed again.
class QcMeshScheduler : public QObject
{
    Q_OBJECT

public:
    // How quickly block changes reach the screen (benchmark): time from a change until every
    // section it made dirty has a new mesh, the largest number of sections and light jobs waiting,
    // and the time the light thread spent.
    struct Stats
    {
        quint64 latencySamples = 0;
        double latencyTotalMs = 0.0;
        double latencyWorstMs = 0.0;
        qsizetype worstBacklog = 0;
        quint64 lightJobs = 0;
        double lightTotalMs = 0.0;
        double lightWorstMs = 0.0;
        // Mesh and light jobs dropped unstarted by clear().
        qsizetype discardedJobs = 0;
        // Worker time per section mesh (results that arrived, outdated ones included).
        quint64 meshJobs = 0;
        double meshTotalMs = 0.0;
        double meshWorstMs = 0.0;
    };

    explicit QcMeshScheduler(QObject *parent = nullptr);
    ~QcMeshScheduler() override;

    void setRenderTable(std::shared_ptr<const QcBlockRenderTable> table);
    void setFocus(const QcVec3 &position) { m_focus = position; }
    // Whether meshes carry the light of their cells (QcMeshInput::sectionLight); for the sections
    // meshed from then on.
    void setSectionLight(bool on) { m_sectionLight = on; }

    void chunkLoaded(const QcClientWorld &world, QcChunkPos pos);
    void chunkUnloaded(QcChunkPos pos);
    // The block at `pos` changed from `previous` to what the world holds now. Light changes of the
    // block changes of one pass of the event loop go to the light engine together, as one job
    // (an explosion's hundreds of blocks, decision E-604).
    void blockChanged(const QcClientWorld &world, QcBlockPos pos, QcBlockId previous);
    void clear();

    // Starts jobs for dirty sections, at most `maxJobsInFlight` at a time.
    void pump(const QcClientWorld &world);

    qsizetype dirtyCount() const { return m_dirty.size(); }
    qsizetype jobsInFlight() const { return m_inFlight.size(); }
    // Light jobs submitted or gathered and not yet back.
    int lightJobsPending() const { return m_lightJobsPending + (m_gathered.isEmpty() ? 0 : 1); }
    // Block light (0..15) as the meshes show it; 0 where it is not known yet.
    int blockLight(QcBlockPos pos) const;
    // Submits the gathered light changes and blocks until all queued jobs finished; their results are
    // delivered through the event loop.
    void waitForJobs();

    Stats stats() const { return m_stats; }
    void resetStats() { m_stats = {}; }

signals:
    void meshReady(quint64 key, std::shared_ptr<const QcChunkMesh> mesh);
    void meshRemoved(quint64 key);

private:
    struct SectionAddress
    {
        QcChunkPos chunk;
        int section = 0;
    };

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
        QList<SectionAddress> dirty;
        // Start of the change in m_clock time, or -1.
        qint64 changedAtNs = -1;
        double workMs = 0.0;
    };

    void markDirty(QcChunkPos chunk, int section, qint64 changedAtNs = -1);
    // Sections whose faces or ambient occlusion a change of the block at `pos` affects.
    static QList<SectionAddress> faceSections(QcBlockPos pos);
    // Sky light travels up to 15 blocks sideways, so it reaches the 3 x 3 chunk neighbourhood. A
    // section's sky light (QcChunkMesher) depends on the cells from 16 blocks below to 16 blocks
    // above it and on where each column's highest light-dimming block lies: it changes when that
    // range holds the changed cell or a cell that turns lit or shadowed because the column's highest
    // dimming block moved. Returns the lowest and highest such cell for a change at `pos`.
    std::pair<int, int> skyLightRange(const QcClientWorld &world, QcBlockPos pos, const QcBlockRenderTable::Entry &before,
                                      const QcBlockRenderTable::Entry &after) const;
    // Dirty sections not being meshed, nearest to the focus first.
    QList<quint64> nearestDirtyFirst() const;
    // Nullopt while a side neighbour of the chunk is missing.
    [[nodiscard]] std::optional<QcMeshInput> meshInput(const QcClientWorld &world, SectionAddress address) const;
    void submitMesh(quint64 key, QcMeshInput input);
    // Records the time since the section's earliest unshown change, if any.
    void noteLatency(quint64 key);
    // The chunk and its loaded neighbours, all sections.
    void markChunkAndNeighbours(QcChunkPos pos);
    void submitLight(std::function<void(QcBlockLightEngine &)> work, std::optional<QcChunkPos> added, QList<SectionAddress> dirty,
                     qint64 changedAtNs);
    void deliverResults();
    void deliverLight();
    // Submits the gathered light changes of block changes as one job.
    void submitGathered();
    void setLight(QcChunkPos chunk, int section, std::shared_ptr<const QcChunkLight::Section> light);
    void noteBacklog();

    std::shared_ptr<const QcBlockRenderTable> m_renderTable;
    QHash<quint64, SectionAddress> m_dirty;
    // Version of the latest dirty mark per section; results of older jobs are discarded.
    QHash<quint64, quint64> m_versions;
    quint64 m_lastVersion = 0;
    QSet<quint64> m_inFlight;
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
    QList<SectionAddress> m_gatheredDirty;
    qint64 m_gatheredSinceNs = -1;
    QcMailbox<LightResult> m_lightResults;
    // Earliest unshown block change per dirty section (latency statistics).
    QHash<quint64, qint64> m_changedAt;
    QElapsedTimer m_clock;
    Stats m_stats;
    // Declared last so they are destroyed first: workers must stop before the members they use.
    QcWorkerPool m_lightPool{1};
    QcWorkerPool m_pool;
};
