#pragma once

#include "QcBlockPos.h"
#include "QcBlockRenderTable.h"
#include "QcChunkMesh.h"
#include "QcMailbox.h"
#include "QcVec3.h"
#include "QcWorkerPool.h"

#include <QHash>
#include <QObject>
#include <QSet>

#include <memory>

class QcClientWorld;

// Keeps section meshes in sync with the client world. Dirty sections are meshed on private
// worker threads from immutable chunk snapshots, nearest to the camera first; results come back to the
// owning thread as signals. A section is only meshed once its four side neighbours are loaded.
class QcMeshScheduler : public QObject
{
    Q_OBJECT

public:
    explicit QcMeshScheduler(QObject *parent = nullptr);
    ~QcMeshScheduler() override;

    void setRenderTable(std::shared_ptr<const QcBlockRenderTable> table);
    void setFocus(const QcVec3 &position) { m_focus = position; }

    void chunkLoaded(const QcClientWorld &world, QcChunkPos pos);
    void chunkUnloaded(QcChunkPos pos);
    void blockChanged(QcBlockPos pos);
    void clear();

    // Starts jobs for dirty sections, at most `maxJobsInFlight` at a time.
    void pump(const QcClientWorld &world);

    qsizetype dirtyCount() const { return m_dirty.size(); }
    qsizetype jobsInFlight() const { return m_inFlight.size(); }
    // Blocks until all queued jobs finished; their results are delivered through the event loop.
    void waitForJobs();

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

    void markDirty(QcChunkPos chunk, int section);
    void deliverResults();

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
    // Declared last so it is destroyed first: workers must stop before the members they use.
    QcWorkerPool m_pool;
};
