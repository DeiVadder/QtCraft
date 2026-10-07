#include "QcMeshScheduler.h"

#include "QcChunkMesher.h"
#include "QcClientWorld.h"
#include "QcSectionKey.h"

#include <QThread>

#include <algorithm>

QcMeshScheduler::QcMeshScheduler(QObject *parent)
    : QObject(parent)
    // Leave cores for the GUI, render and server threads.
    , m_pool(std::max(1, QThread::idealThreadCount() - 3))
{
    m_maxJobsInFlight = m_pool.threadCount() * 2;
}

QcMeshScheduler::~QcMeshScheduler()
{
    m_pool.waitForIdle();
}

void QcMeshScheduler::setRenderTable(std::shared_ptr<const QcBlockRenderTable> table)
{
    m_renderTable = std::move(table);
}

void QcMeshScheduler::markDirty(QcChunkPos chunk, int section)
{
    const quint64 key = qcSectionKey(chunk, section);
    m_dirty.insert(key, {chunk, section});
    m_versions.insert(key, ++m_lastVersion);
}

void QcMeshScheduler::chunkLoaded(const QcClientWorld &world, QcChunkPos pos)
{
    m_loadedChunks.insert(pos);
    for (int section = 0; section < QcWorld::sectionCount; ++section) {
        markDirty(pos, section);
    }
    // Neighbours may now show fewer border faces and different ambient occlusion.
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            const QcChunkPos neighbour{pos.x + dx, pos.z + dz};
            if ((dx != 0 || dz != 0) && world.chunk(neighbour)) {
                for (int section = 0; section < QcWorld::sectionCount; ++section) {
                    markDirty(neighbour, section);
                }
            }
        }
    }
}

void QcMeshScheduler::chunkUnloaded(QcChunkPos pos)
{
    m_loadedChunks.remove(pos);
    for (int section = 0; section < QcWorld::sectionCount; ++section) {
        const quint64 key = qcSectionKey(pos, section);
        m_dirty.remove(key);
        // Versions are globally unique, so dropping the entry also invalidates running jobs.
        m_versions.remove(key);
        emit meshRemoved(key);
    }
}

void QcMeshScheduler::blockChanged(QcBlockPos pos)
{
    // Faces and ambient occlusion only reach one block, but sky light travels up to 15 blocks
    // sideways and down the whole column: every section of the 3 x 3 chunk neighbourhood from the
    // bottom up to one section above the change can be affected.
    const int topSection = std::min(QcWorld::sectionCount - 1, pos.y / QcWorld::sectionHeight + 1);
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            const QcChunkPos chunk{pos.chunk().x + dx, pos.chunk().z + dz};
            if (!m_loadedChunks.contains(chunk)) {
                continue;
            }
            for (int section = 0; section <= topSection; ++section) {
                markDirty(chunk, section);
            }
        }
    }
}

void QcMeshScheduler::clear()
{
    m_versions.clear();
    m_dirty.clear();
    m_loadedChunks.clear();
}

void QcMeshScheduler::pump(const QcClientWorld &world)
{
    if (!m_renderTable || m_dirty.isEmpty() || m_inFlight.size() >= m_maxJobsInFlight) {
        return;
    }
    QList<quint64> candidates;
    candidates.reserve(m_dirty.size());
    for (auto it = m_dirty.constBegin(); it != m_dirty.constEnd(); ++it) {
        if (!m_inFlight.contains(it.key())) {
            candidates.append(it.key());
        }
    }
    const auto distance = [this](SectionAddress address) {
        const double dx = (address.chunk.x + 0.5) * QcWorld::chunkSize - m_focus.x;
        const double dy = (address.section + 0.5) * QcWorld::sectionHeight - m_focus.y;
        const double dz = (address.chunk.z + 0.5) * QcWorld::chunkSize - m_focus.z;
        return dx * dx + dy * dy * 0.25 + dz * dz;
    };
    std::ranges::sort(candidates, [&](quint64 a, quint64 b) { return distance(m_dirty[a]) < distance(m_dirty[b]); });

    for (const quint64 key : std::as_const(candidates)) {
        if (m_inFlight.size() >= m_maxJobsInFlight) {
            break;
        }
        const SectionAddress address = m_dirty.value(key);
        const std::shared_ptr<const QcChunk> centre = world.chunk(address.chunk);
        if (!centre) {
            m_dirty.remove(key);
            continue;
        }
        if (centre->section(address.section).isEmpty()) {
            m_dirty.remove(key);
            emit meshRemoved(key);
            continue;
        }
        QcMeshInput input;
        bool sidesLoaded = true;
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dx = -1; dx <= 1; ++dx) {
                auto chunk = world.chunk({address.chunk.x + dx, address.chunk.z + dz});
                if (!chunk && (dx == 0 || dz == 0)) {
                    sidesLoaded = false;
                }
                const int index = (dz + 1) * 3 + (dx + 1);
                input.chunks[std::size_t(index)] = std::move(chunk);
            }
        }
        if (!sidesLoaded) {
            // Dropped for now: loading the missing neighbour marks this section dirty again.
            m_dirty.remove(key);
            continue;
        }
        input.sectionIndex = address.section;
        input.renderTable = m_renderTable;
        m_dirty.remove(key);
        m_inFlight.insert(key);
        const quint64 version = m_versions.value(key);
        m_pool.submit([this, key, version, input = std::move(input)] {
            auto mesh = std::make_shared<const QcChunkMesh>(QcChunkMesher::mesh(input));
            if (m_results.push({key, version, std::move(mesh)})) {
                QMetaObject::invokeMethod(this, &QcMeshScheduler::deliverResults, Qt::QueuedConnection);
            }
        });
    }
}

void QcMeshScheduler::deliverResults()
{
    for (Result &result : m_results.takeAll()) {
        m_inFlight.remove(result.key);
        if (m_versions.value(result.key) != result.version) {
            continue;
        }
        if (result.mesh->isEmpty()) {
            emit meshRemoved(result.key);
        } else {
            emit meshReady(result.key, std::move(result.mesh));
        }
    }
}

void QcMeshScheduler::waitForJobs()
{
    m_pool.waitForIdle();
}
