#include "server/core/QcServerWorld.h"

#include "common/light/QcLightTable.h"
#include "common/world/QcLightVolume.h"

#include <QLoggingCategory>
#include <QThread>

#include <algorithm>
#include <cmath>

namespace {

Q_LOGGING_CATEGORY(lcWorld, "qtcraft.server")

} // namespace

QcServerWorld::QcServerWorld(std::shared_ptr<const QcContentRegistry> content, std::unique_ptr<QcWorldGenerator> generator)
    : m_content(std::move(content))
    , m_generator(std::move(generator))
    , m_links(*m_content)
    , m_loaders(*m_content)
    , m_villages(std::make_unique<QcVillages>(*m_content, *m_generator))
    , m_blockEntities(m_content)
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
    m_io = io;
    m_catalog = std::move(catalog);
    if (m_io) {
        // Only the immutable light table goes to the I/O thread, which may outlive the world.
        m_io->setPreparation([table = m_light.sharedTable()](Arrival &arrival) {
            arrival.light = QcLightEngine(table, {}).prepare(*arrival.blocks);
        });
    }
}

std::optional<QcServerWorld::Stored> QcServerWorld::stored(QcChunkPos pos) const
{
    const auto it = m_catalog.constFind(pos);
    return it == m_catalog.cend() ? std::nullopt : std::optional<Stored>(*it);
}

quint16 QcServerWorld::linkMarksOf(QcChunkPos pos) const
{
    if (m_chunks.contains(pos)) {
        return m_links.marksOf(pos);
    }
    return m_catalog.value(pos).marks;
}

void QcServerWorld::generateInBackground(QcChunkPos pos, Arrival kept)
{
    kept.request = m_pending.value(pos);
    m_generationPool.submit([this, pos, kept = std::move(kept)]() mutable {
        kept.pos = pos;
        kept.blocks = m_generator->generate(pos);
        kept.light = m_light.prepare(*kept.blocks);
        m_generated.push(std::move(kept));
    });
}

void QcServerWorld::requestChunk(QcChunkPos pos)
{
    if (m_chunks.contains(pos) || m_pending.contains(pos)) {
        return;
    }

    const quint64 request = m_nextRequest++;
    m_pending.insert(pos, request);
    if (m_io && m_catalog.contains(pos)) {
        m_io->read(pos, request);
    } else {
        generateInBackground(pos, {});
    }
}

std::vector<QcServerWorld::Arrival> QcServerWorld::collectArrivals()
{
    std::vector<Arrival> ready;
    std::vector<Arrival> arrived = m_generated.takeAll();
    if (m_io) {
        for (Arrival &read : m_io->takeArrivals()) {
            arrived.push_back(std::move(read));
        }
    }
    for (Arrival &arrival : arrived) {
        // An answer to an older request (the chunk was loaded, left and asked for again) is stale.
        if (m_chunks.contains(arrival.pos) || m_pending.value(arrival.pos) != arrival.request) {
            continue;
        }
        if (!arrival.error.isEmpty()) {
            // Damaged data stays on disk untouched; the chunk plays as generated and is never kept.
            qCCritical(lcWorld) << "Chunk" << arrival.pos.x << arrival.pos.z << "cannot be read, it plays as generated and is not saved:"
                                << arrival.error;
            m_quarantined.insert(arrival.pos);
            m_newlyQuarantined.append(arrival.pos);
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
    if (m_catalog.value(pos).has(QcChunkStore::Kind::Blocks)) {
        m_differs.insert(pos);
    }
    if (m_onArrival && !m_quarantined.contains(pos)) {
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
}

void QcServerWorld::forgetSections(QcChunkPos pos)
{
    m_marks.forgetChunk(pos);
    m_hoppers.forgetChunk(pos);
    m_links.forgetChunk(pos);
    m_fires.forgetChunk(pos);
    m_blockEntityParts.remove(pos);
    m_villages->forgetChunk(pos);
}

std::shared_ptr<const QcChunk> QcServerWorld::chunk(QcChunkPos pos) const
{
    return m_chunks.value(pos);
}

QcServerWorld::Arrival QcServerWorld::produceNow(QcChunkPos pos)
{
    Arrival arrival;
    if (m_io && m_catalog.contains(pos) && !m_quarantined.contains(pos)) {
        arrival = m_io->readNow(pos);
        if (!arrival.error.isEmpty()) {
            qCCritical(lcWorld) << "Chunk" << pos.x << pos.z << "cannot be read, it plays as generated and is not saved:" << arrival.error;
            m_quarantined.insert(pos);
            m_newlyQuarantined.append(pos);
            arrival = {};
        }
    }
    arrival.pos = pos;
    if (!arrival.blocks) {
        arrival.blocks = m_generator->generate(pos);
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
    m_differs.remove(pos);
    // A quarantined chunk is never kept (its stored data stays as it is).
    const bool changed = m_changed.remove(pos) && !m_quarantined.contains(pos);
    return changed ? std::shared_ptr<const QcChunk>(std::move(chunk)) : nullptr;
}

void QcServerWorld::unloadChunk(QcChunkPos pos)
{
    if (m_differs.contains(pos)) {
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
    m_changed.remove(pos);
    m_differs.remove(pos);
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
    // Its damaged data is given up: what plays now replaces it in the next save.
    m_quarantined.remove(pos);
    m_changed.insert(pos);
    m_differs.insert(pos);
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
    if (!m_blockEntityParts.isEmpty()) [[unlikely]] {
        dropOrphanTails(pos);
    }

    m_changed.insert(pos.chunk());
    m_differs.insert(pos.chunk());
    return true;
}

void QcServerWorld::adoptStoredChunks(const QHash<QcChunkPos, std::shared_ptr<QcChunk>> &chunks)
{
    for (auto it = chunks.constBegin(); it != chunks.constEnd(); ++it) {
        m_pending.remove(it.key());
        insert(it.key(), it.value(), nullptr);
        m_changed.insert(it.key());
        m_differs.insert(it.key());
    }
}

std::shared_ptr<const QcChunk> QcServerWorld::takeChanged(QcChunkPos pos)
{
    if (!m_changed.contains(pos) || m_quarantined.contains(pos)) {
        return nullptr;
    }
    m_changed.remove(pos);
    return m_chunks.value(pos);
}

void QcServerWorld::markChanged(QcChunkPos pos)
{
    if (m_chunks.contains(pos)) {
        m_changed.insert(pos);
    }
}

void QcServerWorld::keepBlockEntityParts(QcChunkPos pos, QcBlockEntityKept parts)
{
    if (parts.isEmpty()) {
        m_blockEntityParts.remove(pos);
    } else {
        m_blockEntityParts[pos] = std::move(parts);
    }
}

void QcServerWorld::dropOrphanTails(QcBlockPos pos)
{
    const auto parts = m_blockEntityParts.find(pos.chunk());
    if (parts == m_blockEntityParts.end() || parts->entries.isEmpty()) {
        return;
    }
    const auto tails = parts->entries.find(pos);
    if (tails == parts->entries.end()) {
        return;
    }

    // Scheduled ticks stay with a changed block (QcBlockEntities::placed); the backstop for them
    // and for entries that end without a block change is QcBlockEntityRecords::pruneKept.
    using Table = QcBlockEntityRecord::Table;
    tails->removeIf([this, pos](const QcEntityComponent &tail) {
        switch (Table(tail.id)) {
        case Table::Containers:
            return m_blockEntities.container(pos) == nullptr;
        case Table::Machines:
            return m_blockEntities.machine(pos) == nullptr;
        case Table::Saplings:
            return m_blockEntities.sapling(pos) == nullptr;
        case Table::Memories:
            return m_blockEntities.memory(pos) == nullptr;
        case Table::Fires:
            return !m_fires.contains(pos);
        default:
            return false;
        }
    });
    if (tails->isEmpty()) {
        parts->entries.erase(tails);
    }
    if (parts->isEmpty()) {
        m_blockEntityParts.erase(parts);
    }
}

void QcServerWorld::noteKept(const QcChunkIo::Departure &departure)
{
    using Kind = QcChunkStore::Kind;
    Stored stored = m_catalog.value(departure.pos);
    const auto set = [&stored](Kind kind, bool present) {
        const auto bit = quint8(1u << quint8(kind));
        stored.kinds = present ? quint8(stored.kinds | bit) : quint8(stored.kinds & ~bit);
    };
    if (departure.blocks) {
        set(Kind::Blocks, true);
    }
    set(Kind::BlockEntities, !departure.blockEntities.isEmpty() || departure.marks != 0);
    set(Kind::Entities, !departure.entities.isEmpty() || departure.entities.populated);
    stored.marks = departure.marks;
    if (stored.kinds == 0) {
        m_catalog.remove(departure.pos);
    } else {
        m_catalog.insert(departure.pos, stored);
    }
}

bool QcServerWorld::mayHaveLightSourceNear(QcBlockPos pos, int reach) const
{
    return QcLightVolume::anySourceNear(*m_content, pos, reach, [this](QcChunkPos chunk) { return m_chunks.value(chunk).get(); });
}
