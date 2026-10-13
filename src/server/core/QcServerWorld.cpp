#include "QcServerWorld.h"

#include "QcLightVolume.h"
#include "QcWorldStorage.h"

#include <QThread>

#include <algorithm>
#include <cmath>

QcServerWorld::QcServerWorld(std::shared_ptr<const QcContentRegistry> content, std::unique_ptr<QcWorldGenerator> generator)
    : m_content(std::move(content))
    , m_generator(std::move(generator))
    // Leave cores for the GUI, render, server and meshing threads.
    , m_generationPool(std::max(1, QThread::idealThreadCount() / 3))
{
    const auto stateCount = std::size_t(m_content->stateCount());
    std::vector<bool> damaging(stateCount);
    std::vector<bool> growing(stateCount);
    std::vector<bool> circuits(stateCount);
    std::vector<bool> plates(stateCount);
    std::vector<bool> hoppers(stateCount);
    m_contactDamage.resize(stateCount);
    for (std::size_t id = 0; id < stateCount; ++id) {
        const QcBlockDefinition &block = m_content->block(QcBlockId(id));
        m_contactDamage[id] = block.contactDamage;
        damaging[id] = block.contactDamage > 0.0;
        growing[id] = block.hasRandomTicks();
        // Hoppers react only to power, which comes from components that are marked themselves; a
        // flow of water next to hoppers is no circuit work (decision E-344).
        circuits[id] = block.signal.isComponent() && block.signal.kind != QcSignalKind::Hopper;
        plates[id] = block.signal.kind == QcSignalKind::Plate;
        hoppers[id] = block.signal.kind == QcSignalKind::Hopper;
    }

    m_damagingSections = QcSectionMarks(std::move(damaging));
    m_growingSections = QcSectionMarks(std::move(growing));
    m_signalSections = QcSectionMarks(std::move(circuits));
    m_plateSections = QcSectionMarks(std::move(plates));
    m_hoppers = QcBlockIndex(std::move(hoppers));
}

void QcServerWorld::requestChunk(QcChunkPos pos)
{
    if (m_chunks.contains(pos) || m_pending.contains(pos)) {
        return;
    }

    m_pending.insert(pos);
    m_generationPool.submit([this, pos] { m_generated.push(m_generator->generate(pos)); });
}

QList<QcChunkPos> QcServerWorld::collectGeneratedChunks()
{
    QList<QcChunkPos> collected;
    for (const std::shared_ptr<QcChunk> &chunk : m_generated.takeAll()) {
        const QcChunkPos pos = chunk->pos();
        m_pending.remove(pos);

        // A synchronous load may have won the race; the first chunk stays authoritative.
        if (!m_chunks.contains(pos)) {
            m_chunks.insert(pos, chunk);
            noteSections(pos, *chunk);
            m_newlyLoaded.append(pos);
            collected.append(pos);
        }
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

namespace {

// Inclusive block bounds.
struct BlockBounds
{
    int minX = 0;
    int maxX = 0;
    int minY = 0;
    int maxY = 0;
    int minZ = 0;
    int maxZ = 0;
};

double contactDamageOf(const std::vector<double> &table, QcBlockId id)
{
    return std::size_t(id) < table.size() ? table[std::size_t(id)] : 0.0;
}

// Highest contact damage in `section` within `local` (coordinates local to the section).
double contactDamageInSection(const QcChunkSection &section, const BlockBounds &local, const std::vector<double> &table)
{
    double damage = 0.0;
    for (int x = local.minX; x <= local.maxX; ++x) {
        for (int y = local.minY; y <= local.maxY; ++y) {
            for (int z = local.minZ; z <= local.maxZ; ++z) {
                damage = std::max(damage, contactDamageOf(table, section.block(x, y, z)));
            }
        }
    }
    return damage;
}

// Highest contact damage in the sections of `chunk` marked in `mask`, within `local` (x and z
// local to the chunk, y in the world).
double contactDamageInChunk(const QcChunk &chunk, quint32 mask, const BlockBounds &local, const std::vector<double> &table)
{
    constexpr int height = QcWorld::sectionHeight;
    double damage = 0.0;
    for (int index = local.minY / height; index <= local.maxY / height; ++index) {
        if ((mask & (1u << index)) == 0) {
            continue;
        }

        BlockBounds inSection = local;
        inSection.minY = std::max(local.minY, index * height) - index * height;
        inSection.maxY = std::min(local.maxY, index * height + height - 1) - index * height;
        damage = std::max(damage, contactDamageInSection(chunk.section(index), inSection, table));
    }
    return damage;
}

} // namespace

double QcServerWorld::contactDamageIn(const QcAabb &box) const
{
    constexpr double inset = 1e-3;
    constexpr int size = QcWorld::chunkSize;

    BlockBounds bounds;
    bounds.minX = int(std::floor(box.min.x + inset));
    bounds.maxX = int(std::floor(box.max.x - inset));
    bounds.minZ = int(std::floor(box.min.z + inset));
    bounds.maxZ = int(std::floor(box.max.z - inset));

    // Outside the world height there is only air, which does no damage.
    bounds.minY = std::max(int(std::floor(box.min.y + inset)), 0);
    bounds.maxY = std::min(int(std::floor(box.max.y - inset)), QcWorld::worldHeight - 1);
    if (m_damagingSections.isEmpty() || bounds.minX > bounds.maxX || bounds.minY > bounds.maxY || bounds.minZ > bounds.maxZ) {
        return 0.0;
    }

    double damage = 0.0;
    for (int chunkX = qcFloorDiv(bounds.minX, size); chunkX <= qcFloorDiv(bounds.maxX, size); ++chunkX) {
        for (int chunkZ = qcFloorDiv(bounds.minZ, size); chunkZ <= qcFloorDiv(bounds.maxZ, size); ++chunkZ) {
            const QcChunkPos pos{chunkX, chunkZ};
            const quint32 sections = m_damagingSections.sectionsOf(pos);
            if (sections == 0) {
                continue;
            }

            BlockBounds local = bounds;
            local.minX = std::max(bounds.minX, chunkX * size) - chunkX * size;
            local.maxX = std::min(bounds.maxX, chunkX * size + size - 1) - chunkX * size;
            local.minZ = std::max(bounds.minZ, chunkZ * size) - chunkZ * size;
            local.maxZ = std::min(bounds.maxZ, chunkZ * size + size - 1) - chunkZ * size;
            damage = std::max(damage, contactDamageInChunk(**m_chunks.constFind(pos), sections, local, m_contactDamage));
        }
    }
    return damage;
}

void QcServerWorld::noteSections(QcChunkPos pos, const QcChunk &chunk)
{
    m_damagingSections.noteChunk(pos, chunk);
    m_growingSections.noteChunk(pos, chunk);
    m_signalSections.noteChunk(pos, chunk);
    m_plateSections.noteChunk(pos, chunk);
    m_hoppers.noteChunk(pos, chunk);
}

std::shared_ptr<const QcChunk> QcServerWorld::chunk(QcChunkPos pos) const
{
    return m_chunks.value(pos);
}

std::shared_ptr<const QcChunk> QcServerWorld::loadChunk(QcChunkPos pos)
{
    auto it = m_chunks.find(pos);
    if (it == m_chunks.end()) {
        it = m_chunks.insert(pos, m_generator->generate(pos));
        noteSections(pos, **it);
        m_newlyLoaded.append(pos);
    }
    return *it;
}

void QcServerWorld::unloadChunk(QcChunkPos pos)
{
    if (m_modified.contains(pos)) {
        return;
    }
    m_chunks.remove(pos);
    m_damagingSections.forgetChunk(pos);
    m_growingSections.forgetChunk(pos);
    m_signalSections.forgetChunk(pos);
    m_plateSections.forgetChunk(pos);
    m_hoppers.forgetChunk(pos);
}

std::shared_ptr<QcChunk> QcServerWorld::takeChunk(QcChunkPos pos)
{
    std::shared_ptr<QcChunk> chunk = m_chunks.take(pos);
    m_modified.remove(pos);
    m_damagingSections.forgetChunk(pos);
    m_growingSections.forgetChunk(pos);
    m_signalSections.forgetChunk(pos);
    m_plateSections.forgetChunk(pos);
    m_hoppers.forgetChunk(pos);
    return chunk;
}

bool QcServerWorld::regenerateChunk(QcChunkPos pos)
{
    const auto it = m_chunks.find(pos);
    if (it == m_chunks.end()) {
        return false;
    }

    *it = m_generator->generate(pos);
    m_damagingSections.forgetChunk(pos);
    m_growingSections.forgetChunk(pos);
    m_signalSections.forgetChunk(pos);
    m_plateSections.forgetChunk(pos);
    m_hoppers.forgetChunk(pos);
    noteSections(pos, **it);
    m_modified.insert(pos);
    m_dirtyRegions.insert(QcWorldStorage::regionOf(pos));
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

    (*it)->setBlock(pos.localX(), pos.y, pos.localZ(), block);
    m_damagingSections.noteBlock(pos, block);
    m_growingSections.noteBlock(pos, block);
    m_signalSections.noteBlock(pos, block);
    m_plateSections.noteBlock(pos, block);
    m_hoppers.noteBlock(pos, block);

    m_modified.insert(pos.chunk());
    m_dirtyRegions.insert(QcWorldStorage::regionOf(pos.chunk()));
    return true;
}

void QcServerWorld::adoptStoredChunks(const QHash<QcChunkPos, std::shared_ptr<QcChunk>> &chunks)
{
    for (auto it = chunks.constBegin(); it != chunks.constEnd(); ++it) {
        m_chunks.insert(it.key(), it.value());
        noteSections(it.key(), *it.value());
        m_modified.insert(it.key());
        m_newlyLoaded.append(it.key());
    }
}

void QcServerWorld::markDirty(const QList<QcChunkPos> &chunks)
{
    for (const QcChunkPos &pos : chunks) {
        m_dirtyRegions.insert(QcWorldStorage::regionOf(pos));
    }
}

QHash<QcChunkPos, std::shared_ptr<const QcChunk>> QcServerWorld::takeChunksToSave()
{
    QHash<QcChunkPos, std::shared_ptr<const QcChunk>> chunks;
    for (const QcChunkPos &pos : std::as_const(m_modified)) {
        if (m_dirtyRegions.contains(QcWorldStorage::regionOf(pos))) {
            chunks.insert(pos, m_chunks.value(pos));
        }
    }
    m_dirtyRegions.clear();
    return chunks;
}

bool QcServerWorld::mayHaveLightSourceNear(QcBlockPos pos, int reach) const
{
    return QcLightVolume::anySourceNear(*m_content, pos, reach, [this](QcChunkPos chunk) { return m_chunks.value(chunk).get(); });
}

int QcServerWorld::blockLightAt(QcBlockPos pos) const
{
    return QcLightVolume::blockLightAt(*this, pos);
}
