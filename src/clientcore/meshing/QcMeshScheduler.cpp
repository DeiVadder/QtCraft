#include "clientcore/meshing/QcMeshScheduler.h"

#include "clientcore/meshing/QcChunkMesher.h"
#include "clientcore/session/QcClientWorld.h"
#include "common/core/QcSectionKey.h"
#include "common/world/QcLightPassage.h"

#include <QThread>

#include <algorithm>
#include <utility>

namespace {

// Highest cell sky light coming straight down does not reach when `entry` stands at `y`; -1 when it
// passes. The same rule as the sky light of QcChunkMesher.
int skyTopOf(const QcBlockRenderTable::Entry &entry, int y)
{
    return QcLightPassage::skyTop(QcLightPassage::skyStop(entry.lightOpacity, entry.lightBlockedFaces), y);
}

// The sky top of the column (x, z) of `chunk` from its cells other than `skip`; -1 for none.
int highestDimming(const QcChunk &chunk, const QcBlockRenderTable &table, int x, int z, int skip)
{
    for (int section = QcWorld::sectionCount - 1; section >= 0; --section) {
        if (chunk.section(section).isEmpty()) {
            continue;
        }
        for (int y = (section + 1) * QcWorld::sectionHeight - 1; y >= section * QcWorld::sectionHeight; --y) {
            const QcBlockRenderTable::Entry &entry = table.entry(chunk.block(x, y, z));
            if (y != skip && QcLightPassage::skyStop(entry.lightOpacity, entry.lightBlockedFaces) != QcLightPassage::SkyStop::None) {
                return skyTopOf(entry, y);
            }
        }
    }
    return -1;
}

// Calls `visit(chunk, section)` for the sections of the 3 x 3 chunks around `centre` whose sky light
// the cells low..high reach (whose light volume meets low..high).
template<typename Visit>
void forSkyLightSections(QcChunkPos centre, int low, int high, Visit visit)
{
    constexpr int margin = QcChunkMesher::lightMargin;
    const int firstSection = std::max(0, qcFloorDiv(low - margin, QcWorld::sectionHeight));
    const int lastSection = std::min(QcWorld::sectionCount - 1, qcFloorDiv(high + margin, QcWorld::sectionHeight));
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            for (int index = firstSection; index <= lastSection; ++index) {
                visit(QcChunkPos{centre.x + dx, centre.z + dz}, index);
            }
        }
    }
}

} // namespace

QcMeshScheduler::QcMeshScheduler(QObject *parent)
    : QObject(parent)
    // Leave cores for the GUI, render, server and light threads.
    , m_pool(std::max(1, QThread::idealThreadCount() - 3))
{
    m_maxJobsInFlight = m_pool.threadCount() * 2;
    m_clock.start();
}

QcMeshScheduler::~QcMeshScheduler()
{
    // Queued work is of no use any more; only running jobs (they use this object) are waited for.
    ++m_lightGeneration;
    m_lightPool.discardQueued();
    m_pool.discardQueued();
    m_pool.waitForIdle();
    m_lightPool.waitForIdle();
}

void QcMeshScheduler::setRenderTable(std::shared_ptr<const QcBlockRenderTable> table)
{
    m_renderTable = std::move(table);
    ++m_lightGeneration;
    m_lightJobsPending = 0;
    m_light.clear();
    m_lightEngine = m_renderTable ? std::make_shared<QcBlockLightEngine>(m_renderTable) : nullptr;
}

void QcMeshScheduler::markDirty(QcChunkPos chunk, int section, qint64 changedAtNs)
{
    if (section < 0 || section >= QcWorld::sectionCount || !m_loadedChunks.contains(chunk)) {
        return;
    }
    const quint64 key = qcSectionKey(chunk, section);
    m_dirty.insert(key, {chunk, section});
    m_versions.insert(key, ++m_lastVersion);
    if (changedAtNs >= 0 && !m_changedAt.contains(key)) {
        m_changedAt.insert(key, changedAtNs);
    }
}

void QcMeshScheduler::markChunkAndNeighbours(QcChunkPos pos)
{
    // Neighbours may now show fewer border faces and different ambient occlusion.
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            for (int section = 0; section < QcWorld::sectionCount; ++section) {
                markDirty({pos.x + dx, pos.z + dz}, section);
            }
        }
    }
}

void QcMeshScheduler::submitLight(std::function<void(QcBlockLightEngine &)> work, std::optional<QcChunkPos> added,
                                  QList<SectionAddress> dirty, qint64 changedAtNs)
{
    ++m_lightJobsPending;
    m_lightPool.submit([this, engine = m_lightEngine, generation = m_lightGeneration, work = std::move(work), added,
                        dirty = std::move(dirty), changedAtNs] {
        QElapsedTimer timer;
        timer.start();
        work(*engine);

        LightResult result;
        result.generation = generation;
        result.changes = engine->takeChanges();
        result.addedChunk = added;
        if (added) {
            result.addedLight = std::make_shared<const QcChunkLight>(engine->snapshot(*added));
        }
        result.dirty = dirty;
        result.changedAtNs = changedAtNs;
        result.workMs = double(timer.nsecsElapsed()) / 1e6;

        if (m_lightResults.push(std::move(result))) {
            QMetaObject::invokeMethod(this, &QcMeshScheduler::deliverLight, Qt::QueuedConnection);
        }
    });
}

void QcMeshScheduler::chunkLoaded(const QcClientWorld &world, QcChunkPos pos)
{
    m_loadedChunks.insert(pos);
    if (!m_lightEngine) {
        markChunkAndNeighbours(pos);
        return;
    }
    // Meshed once its light is known (deliverLight).
    m_light.remove(pos);
    std::shared_ptr<const QcChunk> chunk = world.chunk(pos);
    if (!chunk) {
        return;
    }
    submitLight([chunk = std::move(chunk)](QcBlockLightEngine &engine) { engine.addChunk(chunk); }, pos, {}, -1);
}

void QcMeshScheduler::chunkUnloaded(QcChunkPos pos)
{
    // Gathered changes of the chunk go first, so the engine never sees them after the removal.
    submitGathered();
    m_loadedChunks.remove(pos);
    m_light.remove(pos);
    for (int section = 0; section < QcWorld::sectionCount; ++section) {
        const quint64 key = qcSectionKey(pos, section);
        m_dirty.remove(key);
        m_changedAt.remove(key);
        // Versions are globally unique, so dropping the entry also invalidates running jobs.
        m_versions.remove(key);
        emit meshRemoved(key);
    }
    if (m_lightEngine) {
        submitLight([pos](QcBlockLightEngine &engine) { engine.removeChunk(pos); }, std::nullopt, {}, -1);
    }
}

QList<QcMeshScheduler::SectionAddress> QcMeshScheduler::faceSections(QcBlockPos pos)
{
    // Faces and ambient occlusion reach the sections holding a cell next to the block.
    QList<SectionAddress> sections;
    const int lx = pos.localX();
    const int lz = pos.localZ();
    const int ly = pos.y % QcWorld::sectionHeight;
    const int section = pos.y / QcWorld::sectionHeight;
    for (int dx = lx == 0 ? -1 : 0; dx <= (lx == QcWorld::chunkSize - 1 ? 1 : 0); ++dx) {
        for (int dz = lz == 0 ? -1 : 0; dz <= (lz == QcWorld::chunkSize - 1 ? 1 : 0); ++dz) {
            for (int dy = ly == 0 ? -1 : 0; dy <= (ly == QcWorld::sectionHeight - 1 ? 1 : 0); ++dy) {
                sections.append({{pos.chunk().x + dx, pos.chunk().z + dz}, section + dy});
            }
        }
    }
    return sections;
}

std::pair<int, int> QcMeshScheduler::skyLightRange(const QcClientWorld &world, QcBlockPos pos, const QcBlockRenderTable::Entry &before,
                                                   const QcBlockRenderTable::Entry &after) const
{
    int low = pos.y;
    int high = pos.y;
    const std::shared_ptr<const QcChunk> chunk = world.chunk(pos.chunk());
    if (!chunk) {
        return {0, high};
    }
    const int others = highestDimming(*chunk, *m_renderTable, pos.localX(), pos.localZ(), pos.y);
    const int topBefore = std::max(others, skyTopOf(before, pos.y));
    const int topAfter = std::max(others, skyTopOf(after, pos.y));
    if (topBefore != topAfter) {
        low = std::min(low, std::min(topBefore, topAfter) + 1);
        high = std::max({high, topBefore, topAfter});
    }
    return {low, high};
}

void QcMeshScheduler::blockChanged(const QcClientWorld &world, QcBlockPos pos, QcBlockId previous)
{
    const qint64 now = m_clock.nsecsElapsed();
    const QcBlockId current = world.blockAt(pos).value_or(qcAirBlockId);
    QList<SectionAddress> dirty = faceSections(pos);

    bool lightChanges = true;
    if (m_renderTable) {
        const QcBlockRenderTable::Entry &before = m_renderTable->entry(previous);
        const QcBlockRenderTable::Entry &after = m_renderTable->entry(current);
        if (before.lightDiffers(after)) {
            const auto [low, high] = skyLightRange(world, pos, before, after);
            forSkyLightSections(pos.chunk(), low, high, [&dirty](QcChunkPos chunk, int section) { dirty.append({chunk, section}); });
        }
        lightChanges = before.lightDiffers(after) || before.lightLevel != after.lightLevel;
    }

    if (m_lightEngine && lightChanges) {
        // Meshed together with the new block light, so the change shows in one step; gathered until
        // the event loop comes back, so the changes of one message (a batch) make one job.
        if (m_gathered.isEmpty()) {
            m_gatheredSinceNs = now;
            QMetaObject::invokeMethod(this, &QcMeshScheduler::submitGathered, Qt::QueuedConnection);
        }
        m_gathered.append({pos, world.chunk(pos.chunk())});
        m_gatheredDirty.append(dirty);
        return;
    }
    for (const SectionAddress &address : std::as_const(dirty)) {
        markDirty(address.chunk, address.section, now);
    }
}

void QcMeshScheduler::submitGathered()
{
    if (m_gathered.isEmpty() || !m_lightEngine) {
        m_gathered.clear();
        m_gatheredDirty.clear();
        return;
    }

    submitLight(
        [changes = std::exchange(m_gathered, {})](QcBlockLightEngine &engine) {
            for (const auto &[pos, chunk] : changes) {
                engine.changeBlock(pos, chunk);
            }
        },
        std::nullopt, std::exchange(m_gatheredDirty, {}), m_gatheredSinceNs);
}

void QcMeshScheduler::clear()
{
    m_gathered.clear();
    m_gatheredDirty.clear();
    m_versions.clear();
    m_dirty.clear();
    m_loadedChunks.clear();
    m_changedAt.clear();
    m_light.clear();
    // Waiting jobs belong to the old world: drop them instead of working through them. Results of
    // running ones are discarded by version and generation when they arrive.
    m_stats.discardedJobs += m_pool.discardQueued() + m_lightPool.discardQueued();
    m_inFlight.clear();
    ++m_lightGeneration;
    m_lightJobsPending = 0;
    if (m_renderTable) {
        m_lightEngine = std::make_shared<QcBlockLightEngine>(m_renderTable);
    }
}

void QcMeshScheduler::setLight(QcChunkPos chunk, int section, std::shared_ptr<const QcChunkLight::Section> light)
{
    const auto it = m_light.find(chunk);
    if (it == m_light.end()) {
        return;
    }
    // Copy on write: running mesh jobs keep the snapshot they were given.
    auto copy = std::make_shared<QcChunkLight>(**it);
    copy->sections[std::size_t(section)] = std::move(light);
    *it = std::move(copy);
}

void QcMeshScheduler::deliverLight()
{
    for (LightResult &result : m_lightResults.takeAll()) {
        if (result.generation != m_lightGeneration) {
            continue;
        }

        --m_lightJobsPending;
        ++m_stats.lightJobs;
        m_stats.lightTotalMs += result.workMs;
        m_stats.lightWorstMs = std::max(m_stats.lightWorstMs, result.workMs);

        for (const QcBlockLightEngine::SectionUpdate &update : std::as_const(result.changes.sections)) {
            setLight(update.chunk, update.section, update.light);
        }

        if (const std::optional<QcChunkPos> added = result.addedChunk) {
            if (m_loadedChunks.contains(*added)) {
                m_light.insert(*added, result.addedLight);
                markChunkAndNeighbours(*added);
            }
        }

        for (const QcBlockLightEngine::SectionRef &ref : std::as_const(result.changes.remesh)) {
            markDirty(ref.chunk, ref.section, result.changedAtNs);
        }
        for (const SectionAddress &address : std::as_const(result.dirty)) {
            markDirty(address.chunk, address.section, result.changedAtNs);
        }
    }

    noteBacklog();
}

int QcMeshScheduler::blockLight(QcBlockPos pos) const
{
    const auto it = m_light.constFind(pos.chunk());
    return it == m_light.constEnd() || !*it ? 0 : (*it)->level(pos.localX(), pos.y, pos.localZ());
}

void QcMeshScheduler::noteBacklog()
{
    m_stats.worstBacklog = std::max(m_stats.worstBacklog, m_dirty.size() + m_inFlight.size() + m_lightJobsPending);
}

QList<quint64> QcMeshScheduler::nearestDirtyFirst() const
{
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

    return candidates;
}

std::optional<QcMeshInput> QcMeshScheduler::meshInput(const QcClientWorld &world, SectionAddress address) const
{
    QcMeshInput input;
    bool sidesLoaded = true;
    for (int dz = -1; dz <= 1; ++dz) {
        for (int dx = -1; dx <= 1; ++dx) {
            const QcChunkPos pos{address.chunk.x + dx, address.chunk.z + dz};
            auto chunk = world.chunk(pos);
            if (!chunk && (dx == 0 || dz == 0)) {
                sidesLoaded = false;
            }
            const int index = (dz + 1) * 3 + (dx + 1);
            input.chunks[std::size_t(index)] = std::move(chunk);
            input.blockLight[std::size_t(index)] = m_light.value(pos);
        }
    }
    if (!sidesLoaded) {
        return std::nullopt;
    }

    input.sectionIndex = address.section;
    input.renderTable = m_renderTable;
    input.hasBlockLight = m_lightEngine != nullptr;
    input.sectionLight = m_sectionLight;
    return input;
}

void QcMeshScheduler::submitMesh(quint64 key, QcMeshInput input)
{
    m_dirty.remove(key);
    m_inFlight.insert(key);
    const quint64 version = m_versions.value(key);
    m_pool.submit([this, key, version, input = std::move(input)] {
        QElapsedTimer work;
        work.start();
        auto mesh = std::make_shared<const QcChunkMesh>(QcChunkMesher::mesh(input));
        if (m_results.push({key, version, std::move(mesh), double(work.nsecsElapsed()) / 1e6})) {
            QMetaObject::invokeMethod(this, &QcMeshScheduler::deliverResults, Qt::QueuedConnection);
        }
    });
}

void QcMeshScheduler::noteLatency(quint64 key)
{
    if (const auto changed = m_changedAt.constFind(key); changed != m_changedAt.constEnd()) {
        const double ms = double(m_clock.nsecsElapsed() - *changed) / 1e6;
        ++m_stats.latencySamples;
        m_stats.latencyTotalMs += ms;
        m_stats.latencyWorstMs = std::max(m_stats.latencyWorstMs, ms);
        m_changedAt.remove(key);
    }
}

void QcMeshScheduler::pump(const QcClientWorld &world)
{
    noteBacklog();
    if (!m_renderTable || m_dirty.isEmpty() || m_inFlight.size() >= m_maxJobsInFlight) {
        return;
    }

    const QList<quint64> candidates = nearestDirtyFirst();
    for (const quint64 key : candidates) {
        if (m_inFlight.size() >= m_maxJobsInFlight) {
            break;
        }
        const SectionAddress address = m_dirty.value(key);
        const std::shared_ptr<const QcChunk> centre = world.chunk(address.chunk);
        if (!centre || (m_lightEngine && !m_light.contains(address.chunk))) {
            // Without its light yet: the light result marks it again.
            m_dirty.remove(key);
            m_changedAt.remove(key);
            continue;
        }
        if (centre->section(address.section).isEmpty()) {
            m_dirty.remove(key);
            noteLatency(key);
            emit meshRemoved(key);
            continue;
        }
        std::optional<QcMeshInput> input = meshInput(world, address);
        if (!input) {
            // Dropped for now: loading the missing neighbour marks this section dirty again.
            m_dirty.remove(key);
            m_changedAt.remove(key);
            continue;
        }
        submitMesh(key, std::move(*input));
    }
}

void QcMeshScheduler::deliverResults()
{
    for (Result &result : m_results.takeAll()) {
        m_inFlight.remove(result.key);
        ++m_stats.meshJobs;
        m_stats.meshTotalMs += result.workMs;
        m_stats.meshWorstMs = std::max(m_stats.meshWorstMs, result.workMs);
        if (m_versions.value(result.key) != result.version) {
            continue;
        }

        noteLatency(result.key);
        if (result.mesh->isEmpty()) {
            emit meshRemoved(result.key);
        } else {
            emit meshReady(result.key, std::move(result.mesh));
        }
    }
}

void QcMeshScheduler::waitForJobs()
{
    submitGathered();
    m_lightPool.waitForIdle();
    m_pool.waitForIdle();
}
