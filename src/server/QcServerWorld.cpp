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
    m_contactDamage.resize(std::size_t(m_content->stateCount()));
    for (qsizetype id = 0; id < m_content->stateCount(); ++id) {
        m_contactDamage[std::size_t(id)] = m_content->block(QcBlockId(id)).contactDamage;
    }
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
            noteDamagingSections(pos, *chunk);
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

double QcServerWorld::contactDamageIn(const QcAabb &box) const
{
    constexpr double inset = 1e-3;
    constexpr int size = QcWorld::chunkSize;
    constexpr int height = QcWorld::sectionHeight;
    const int minX = int(std::floor(box.min.x + inset));
    const int maxX = int(std::floor(box.max.x - inset));
    const int minZ = int(std::floor(box.min.z + inset));
    const int maxZ = int(std::floor(box.max.z - inset));
    // Outside the world height there is only air, which does no damage.
    const int minY = std::max(int(std::floor(box.min.y + inset)), 0);
    const int maxY = std::min(int(std::floor(box.max.y - inset)), QcWorld::worldHeight - 1);
    if (m_damagingSections.isEmpty() || minX > maxX || minY > maxY || minZ > maxZ) {
        return 0.0;
    }
    const auto damageOf = [this](QcBlockId id) { return std::size_t(id) < m_contactDamage.size() ? m_contactDamage[std::size_t(id)] : 0.0; };
    double damage = 0.0;
    for (int chunkX = qcFloorDiv(minX, size); chunkX <= qcFloorDiv(maxX, size); ++chunkX) {
        for (int chunkZ = qcFloorDiv(minZ, size); chunkZ <= qcFloorDiv(maxZ, size); ++chunkZ) {
            const QcChunkPos pos{chunkX, chunkZ};
            const auto mask = m_damagingSections.constFind(pos);
            if (mask == m_damagingSections.constEnd()) {
                continue;
            }
            const QcChunk &chunk = **m_chunks.constFind(pos);
            const int fromX = std::max(minX, chunkX * size) - chunkX * size;
            const int toX = std::min(maxX, chunkX * size + size - 1) - chunkX * size;
            const int fromZ = std::max(minZ, chunkZ * size) - chunkZ * size;
            const int toZ = std::min(maxZ, chunkZ * size + size - 1) - chunkZ * size;
            for (int index = minY / height; index <= maxY / height; ++index) {
                if ((*mask & (1u << index)) == 0) {
                    continue;
                }
                const QcChunkSection &section = chunk.section(index);
                const int fromY = std::max(minY, index * height) - index * height;
                const int toY = std::min(maxY, index * height + height - 1) - index * height;
                for (int x = fromX; x <= toX; ++x) {
                    for (int y = fromY; y <= toY; ++y) {
                        for (int z = fromZ; z <= toZ; ++z) {
                            damage = std::max(damage, damageOf(section.block(x, y, z)));
                        }
                    }
                }
            }
        }
    }
    return damage;
}

void QcServerWorld::noteDamagingSections(QcChunkPos pos, const QcChunk &chunk)
{
    const auto damaging = [this](QcBlockId id) { return std::size_t(id) < m_contactDamage.size() && m_contactDamage[std::size_t(id)] > 0.0; };
    quint32 mask = 0;
    for (int index = 0; index < QcWorld::sectionCount; ++index) {
        if (std::ranges::any_of(chunk.section(index).palette(), damaging)) {
            mask |= 1u << index;
        }
    }
    if (mask != 0) {
        m_damagingSections.insert(pos, mask);
    } else {
        m_damagingSections.remove(pos);
    }
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
        noteDamagingSections(pos, **it);
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
    m_damagingSections.remove(pos);
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
    if (m_contactDamage[std::size_t(block)] > 0.0) {
        m_damagingSections[pos.chunk()] |= 1u << (pos.y / QcWorld::sectionHeight);
    }
    m_modified.insert(pos.chunk());
    m_dirtyRegions.insert(QcWorldStorage::regionOf(pos.chunk()));
    return true;
}

void QcServerWorld::adoptStoredChunks(const QHash<QcChunkPos, std::shared_ptr<QcChunk>> &chunks)
{
    for (auto it = chunks.constBegin(); it != chunks.constEnd(); ++it) {
        m_chunks.insert(it.key(), it.value());
        noteDamagingSections(it.key(), *it.value());
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
