#include "server/core/QcServerWorld.h"

#include "common/light/QcLightTable.h"
#include "common/world/QcLightVolume.h"

#include <QThread>

#include <algorithm>
#include <cmath>

QcServerWorld::QcServerWorld(std::shared_ptr<const QcContentRegistry> content, std::unique_ptr<QcWorldGenerator> generator)
    : m_content(std::move(content))
    , m_generator(std::move(generator))
    , m_links(*m_content)
    , m_loaders(*m_content)
    , m_villages(std::make_unique<QcVillages>(*m_content, *m_generator))
    , m_blockEntities(m_content)
    , m_spawners(*m_content)
    , m_campfires(*m_content)
    , m_signs(*m_content)
    , m_light(std::make_shared<const QcLightTable>(*m_content), {})
    // Leave cores for the GUI, render, server and meshing threads.
    , m_generationPool(std::max(1, QThread::idealThreadCount() / 3))
{
    const auto stateCount = std::size_t(m_content->stateCount());
    std::vector<QcSectionMarks::Traits> traits(stateCount);
    std::vector<bool> hoppers(stateCount);
    std::vector<bool> fires(stateCount);
    std::vector<bool> eternalGround(stateCount);
    for (std::size_t id = 0; id < stateCount; ++id) {
        const QcBlockDefinition &block = m_content->block(QcBlockId(id));
        traits[id] = QcSectionMarks::traitsOf(block);
        hoppers[id] = block.signal.kind == QcSignalKind::Hopper;
        fires[id] = block.fire.isFire;
        eternalGround[id] = block.fire.eternal;
    }

    m_marks = QcSectionMarks(std::move(traits));
    m_hoppers = QcBlockIndex(std::move(hoppers));
    m_fires = QcFireQueue(std::move(fires), std::move(eternalGround));
    m_villages->setLoadedQuery([this](QcChunkPos pos) { return m_chunks.contains(pos); });
}

void QcServerWorld::setStorage(QcChunkIo *io, QHash<QcChunkPos, Stored> catalog)
{
    m_saves.setStorage(io, std::move(catalog));
    if (io) {
        // Only the immutable light table goes to the I/O thread, which may outlive the world.
        io->setPreparation([table = m_light.sharedTable()](Arrival &arrival) {
            arrival.light = QcLightEngine(table, {}).prepare(*arrival.blocks);
        });
    }
}

quint16 QcServerWorld::linkMarksOf(QcChunkPos pos) const
{
    if (m_chunks.contains(pos)) {
        return m_links.marksOf(pos);
    }
    return m_saves.storedMarks(pos);
}

void QcServerWorld::generateInBackground(QcChunkPos pos, Arrival kept)
{
    kept.request = m_pending.value(pos);
    m_generationPool.submit([this, pos, kept = std::move(kept)]() mutable {
        kept.pos = pos;
        kept.blocks = m_generator->generate(pos);
        kept.generated = true;
        kept.light = m_light.prepare(*kept.blocks);
        m_generated.push(std::move(kept));
    });
}

void QcServerWorld::planGuardSitesInBackground(QcBlockPos from, int reach)
{
    if (m_planningSites->exchange(true)) {
        return;
    }
    m_generationPool.submit([this, from, reach, planning = m_planningSites]() {
        // Only for its plans, which the generator keeps.
        m_generator->guardSitesNear(from, reach);
        planning->store(false);
    });
}

void QcServerWorld::requestChunk(QcChunkPos pos)
{
    if (m_chunks.contains(pos) || m_pending.contains(pos)) {
        return;
    }

    const quint64 request = m_nextRequest++;
    m_pending.insert(pos, request);
    if (m_saves.hasStored(pos)) {
        m_saves.storage()->read(pos, request);
    } else {
        generateInBackground(pos, {});
    }
}

std::vector<QcServerWorld::Arrival> QcServerWorld::collectArrivals()
{
    std::vector<Arrival> ready;
    std::vector<Arrival> arrived = m_generated.takeAll();
    if (QcChunkIo *io = m_saves.storage()) {
        for (Arrival &read : io->takeArrivals()) {
            arrived.push_back(std::move(read));
        }
    }
    for (Arrival &arrival : arrived) {
        // An answer to an older request (the chunk was loaded, left and asked for again) is stale.
        if (m_chunks.contains(arrival.pos) || m_pending.value(arrival.pos) != arrival.request) {
            continue;
        }
        if (!arrival.error.isEmpty()) {
            m_saves.quarantine(arrival.pos, arrival.error);
            generateInBackground(arrival.pos, {});
            continue;
        }
        if (!arrival.blocks) {
            // Only its block entities or entities were kept: its blocks are generated.
            const QcChunkPos pos = arrival.pos;
            generateInBackground(pos, std::move(arrival));
            continue;
        }
        ready.push_back(std::move(arrival));
    }
    return ready;
}

void QcServerWorld::insert(QcChunkPos pos, const std::shared_ptr<QcChunk> &chunk, const std::shared_ptr<QcLightEngine::Prepared> &light)
{
    m_chunks.insert(pos, chunk);
    m_light.addChunk(*chunk, light);
    ++m_loadedEpoch;
    noteSections(pos, *m_chunks.value(pos));
    m_newlyLoaded.append(pos);
}

void QcServerWorld::admit(Arrival &&arrival)
{
    const QcChunkPos pos = arrival.pos;
    m_pending.remove(pos);
    if (m_chunks.contains(pos)) {
        return;
    }
    insert(pos, arrival.blocks, arrival.light);
    m_saves.noteArrived(pos);
    if (m_onArrival && !m_saves.isQuarantined(pos)) {
        m_onArrival(arrival);
    }
}

QList<QcChunkPos> QcServerWorld::collectGeneratedChunks()
{
    QList<QcChunkPos> collected;
    for (Arrival &arrival : collectArrivals()) {
        collected.append(arrival.pos);
        admit(std::move(arrival));
    }
    return collected;
}

std::optional<QcBlockId> QcServerWorld::blockAt(QcBlockPos pos) const
{
    if (pos.y >= QcWorld::worldHeight) {
        return qcAirBlockId;
    }

    const auto it = m_chunks.constFind(pos.chunk());
    if (it == m_chunks.constEnd()) {
        return std::nullopt;
    }
    return (*it)->block(pos.localX(), pos.y, pos.localZ());
}

void QcServerWorld::noteSections(QcChunkPos pos, const QcChunk &chunk)
{
    m_marks.noteChunk(pos, chunk);
    m_hoppers.noteChunk(pos, chunk);
    m_links.noteChunk(chunk);
    m_loaders.noteChunk(chunk);
    m_villages->noteChunk(chunk);
    // The generator's beds and workstations outside its villages count as placed (decision E-490);
    // one broken since is air and counts no more, one listed already stays as it is.
    const QList<QcBlockPos> points = m_generator->pointsOfInterestIn(pos);
    for (const QcBlockPos &point : points) {
        m_villages->notePointOfInterest(point, chunk.block(point.localX(), point.y, point.localZ()));
    }
    m_blockEntities.chunkArrived(chunk, m_marks.sectionsOf(pos, QcBlockTrait::BlockEntity));
    m_fires.noteChunk(pos, chunk, m_marks.sectionsOf(pos, QcBlockTrait::Fire));
    m_spawners.noteChunk(pos, chunk, m_marks.sectionsOf(pos, QcBlockTrait::Spawner));
}

void QcServerWorld::forgetSections(QcChunkPos pos)
{
    m_marks.forgetChunk(pos);
    m_hoppers.forgetChunk(pos);
    m_links.forgetChunk(pos);
    m_fires.forgetChunk(pos);
    m_spawners.forgetChunk(pos);
    m_campfires.forgetChunk(pos);
    m_signs.forgetChunk(pos);
    m_blockEntityExtras.forgetChunk(pos);
    m_villages->forgetChunk(pos);
}

std::shared_ptr<const QcChunk> QcServerWorld::chunk(QcChunkPos pos) const
{
    return m_chunks.value(pos);
}

QcServerWorld::Arrival QcServerWorld::produceNow(QcChunkPos pos)
{
    Arrival arrival;
    if (m_saves.hasStored(pos) && !m_saves.isQuarantined(pos)) {
        arrival = m_saves.storage()->readNow(pos);
        if (!arrival.error.isEmpty()) {
            m_saves.quarantine(pos, arrival.error);
            arrival = {};
        }
    }
    arrival.pos = pos;
    if (!arrival.blocks) {
        arrival.blocks = m_generator->generate(pos);
        arrival.generated = true;
    }
    // A background request of the same chunk is superseded; its result is dropped when it comes.
    arrival.request = m_nextRequest++;
    m_pending.insert(pos, arrival.request);
    return arrival;
}

std::shared_ptr<const QcChunk> QcServerWorld::loadChunk(QcChunkPos pos)
{
    if (const auto it = m_chunks.constFind(pos); it != m_chunks.cend()) {
        return *it;
    }
    if (m_loadGroup) {
        m_loadGroup(pos);
    }
    if (!m_chunks.contains(pos)) {
        admit(produceNow(pos));
    }
    return m_chunks.value(pos);
}

std::shared_ptr<const QcChunk> QcServerWorld::remove(QcChunkPos pos)
{
    std::shared_ptr<QcChunk> chunk = m_chunks.take(pos);
    if (!chunk) {
        return nullptr;
    }
    Q_ASSERT(!m_blockEntities.chunkIndex(pos));
    m_light.removeChunk(pos);
    ++m_loadedEpoch;
    forgetSections(pos);
    const bool changed = m_saves.noteLeft(pos);
    return changed ? std::shared_ptr<const QcChunk>(std::move(chunk)) : nullptr;
}

void QcServerWorld::unloadChunk(QcChunkPos pos)
{
    if (m_saves.differsFromGenerated(pos)) {
        return;
    }
    // The light engine lets go of the chunk's column first, while the chunk still exists.
    if (m_chunks.contains(pos)) {
        m_light.removeChunk(pos);
        m_chunks.remove(pos);
        ++m_loadedEpoch;
    }
    forgetSections(pos);
    m_blockEntities.chunkLeft(pos);
}

std::shared_ptr<QcChunk> QcServerWorld::takeChunk(QcChunkPos pos)
{
    std::shared_ptr<QcChunk> chunk = m_chunks.take(pos);
    m_light.removeChunk(pos);
    ++m_loadedEpoch;
    // Nothing is kept of it.
    m_saves.noteLeft(pos);
    forgetSections(pos);
    return chunk;
}

bool QcServerWorld::regenerateChunk(QcChunkPos pos)
{
    const auto it = m_chunks.find(pos);
    if (it == m_chunks.end()) {
        return false;
    }

    m_light.removeChunk(pos);
    *it = m_generator->generate(pos);
    m_light.addChunk(**it);
    forgetSections(pos);
    m_blockEntities.forgetChunk(pos);
    noteSections(pos, **it);
    m_saves.noteRegenerated(pos);
    return true;
}

bool QcServerWorld::setBlock(QcBlockPos pos, QcBlockId block)
{
    if (!pos.isInsideWorldHeight() || !m_content->isValidBlock(block)) {
        return false;
    }

    auto it = m_chunks.find(pos.chunk());
    if (it == m_chunks.end()) {
        return false;
    }

    if (it->use_count() > 1) {
        *it = std::make_shared<QcChunk>(**it);
    }

    const QcBlockId before = (*it)->block(pos.localX(), pos.y, pos.localZ());
    (*it)->setBlock(pos.localX(), pos.y, pos.localZ(), block);
    m_light.changeBlock(pos, before, **it);
    m_marks.noteBlock(pos, block);
    m_hoppers.noteBlock(pos, block);
    m_links.noteBlock(pos, before, block);
    m_loaders.noteBlock(pos, before, block);
    m_villages->noteBlock(pos, before, block);
    m_blockEntities.placed(pos, before, block);
    m_fires.noteBlock(pos, before, block);
    m_spawners.noteBlock(pos, before, block);
    m_campfires.noteBlock(pos, block);
    m_signs.noteBlock(pos, block);
    if (m_blockEntityExtras.hasKeptParts()) [[unlikely]] {
        m_blockEntityExtras.dropOrphanTails(pos, {m_blockEntities, m_fires, m_spawners, m_signs, m_campfires});
    }

    m_saves.noteChanged(pos.chunk());
    return true;
}

void QcServerWorld::adoptStoredChunks(const QHash<QcChunkPos, std::shared_ptr<QcChunk>> &chunks)
{
    for (auto it = chunks.constBegin(); it != chunks.constEnd(); ++it) {
        m_pending.remove(it.key());
        insert(it.key(), it.value(), nullptr);
        m_saves.noteChanged(it.key());
    }
}

std::shared_ptr<const QcChunk> QcServerWorld::takeChanged(QcChunkPos pos)
{
    return m_saves.takeChanged(pos) ? m_chunks.value(pos) : nullptr;
}

bool QcServerWorld::mayHaveLightSourceNear(QcBlockPos pos, int reach) const
{
    return QcLightVolume::anySourceNear(*m_content, pos, reach, [this](QcChunkPos chunk) { return m_chunks.value(chunk).get(); });
}
