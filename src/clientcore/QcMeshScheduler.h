#pragma once

#include "QcBlockLightEngine.h"
#include "QcBlockPos.h"
#include "QcBlockRenderTable.h"
#include "QcChunkLight.h"
#include "QcChunkMesh.h"
#include "QcMailbox.h"
#include "QcVec3.h"
#include "QcWorkerPool.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSet>

#include <functional>
#include <memory>

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
    };

    explicit QcMeshScheduler(QObject *parent = nullptr);
    ~QcMeshScheduler() override;

    void setRenderTable(std::shared_ptr<const QcBlockRenderTable> table);
    void setFocus(const QcVec3 &position) { m_focus = position; }

    void chunkLoaded(const QcClientWorld &world, QcChunkPos pos);
    void chunkUnloaded(QcChunkPos pos);
    // The block at `pos` changed from `previous` to what the world holds now.
    void blockChanged(const QcClientWorld &world, QcBlockPos pos, QcBlockId previous);
    void clear();

    // Starts jobs for dirty sections, at most `maxJobsInFlight` at a time.
    void pump(const QcClientWorld &world);

    qsizetype dirtyCount() const { return m_dirty.size(); }
    qsizetype jobsInFlight() const { return m_inFlight.size(); }
    int lightJobsPending() const { return m_lightJobsPending; }
    // Block light (0..15) as the meshes show it; 0 where it is not known yet.
    int blockLight(QcBlockPos pos) const;
    // Blocks until all queued jobs finished; their results are delivered through the event loop.
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
    // The chunk and its loaded neighbours, all sections.
    void markChunkAndNeighbours(QcChunkPos pos);
    void submitLight(std::function<void(QcBlockLightEngine &)> work, std::optional<QcChunkPos> added, QList<SectionAddress> dirty,
                     qint64 changedAtNs);
    void deliverResults();
    void deliverLight();
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
    QcMailbox<Result> m_results;
    // Block light per chunk as published by the engine; present once the chunk's light is known.
    QHash<QcChunkPos, std::shared_ptr<const QcChunkLight>> m_light;
    std::shared_ptr<QcBlockLightEngine> m_lightEngine;
    // Results of an engine replaced by setRenderTable() or clear() are dropped.
    quint64 m_lightGeneration = 0;
    int m_lightJobsPending = 0;
    QcMailbox<LightResult> m_lightResults;
    // Earliest unshown block change per dirty section (latency statistics).
    QHash<quint64, qint64> m_changedAt;
    QElapsedTimer m_clock;
    Stats m_stats;
    // Declared last so they are destroyed first: workers must stop before the members they use.
    QcWorkerPool m_lightPool{1};
    QcWorkerPool m_pool;
};
