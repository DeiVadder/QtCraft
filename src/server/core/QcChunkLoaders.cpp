#include "QcChunkLoaders.h"

#include <QSet>

#include <algorithm>

QcChunkLoaders::QcChunkLoaders(const QcContentRegistry &content)
{
    const auto stateCount = std::size_t(content.stateCount());
    m_loader.resize(stateCount);
    const QString loaders = QString::fromLatin1(tag);
    for (std::size_t id = 0; id < stateCount; ++id) {
        const QStringList &tags = content.block(QcBlockId(id)).itemTags;
        m_loader[id] = tags.contains(loaders) || tags.contains(loaders.section(u':', 1));
    }
}

void QcChunkLoaders::noteBlock(QcBlockPos pos, QcBlockId before, QcBlockId after)
{
    if (isLoader(before) == isLoader(after)) {
        return;
    }
    if (isLoader(after)) {
        add(pos, {});
    } else {
        remove(pos);
    }
}

void QcChunkLoaders::noteChunk(const QcChunk &chunk)
{
    const QcChunkPos pos = chunk.pos();
    QSet<QcBlockPos> found;
    for (int s = 0; s < QcWorld::sectionCount; ++s) {
        const QcChunkSection &section = chunk.section(s);
        if (std::ranges::any_of(section.palette(), [this](QcBlockId id) { return isLoader(id); })) {
            findIn(section, {pos.x * QcWorld::chunkSize, s * QcWorld::sectionHeight, pos.z * QcWorld::chunkSize}, found);
        }
    }

    QList<QcBlockPos> gone;
    for (auto it = m_byPos.cbegin(); it != m_byPos.cend(); ++it) {
        if (it.key().chunk() == pos && !found.contains(it.key())) {
            gone.append(it.key());
        }
    }
    for (const QcBlockPos &block : std::as_const(gone)) {
        remove(block);
    }
    QList<QcBlockPos> fresh(found.begin(), found.end());
    std::ranges::sort(fresh);
    for (const QcBlockPos &block : std::as_const(fresh)) {
        if (!m_byPos.contains(block)) {
            add(block, {});
        }
    }
}

void QcChunkLoaders::findIn(const QcChunkSection &section, QcBlockPos corner, QSet<QcBlockPos> &found) const
{
    constexpr int side = QcWorld::chunkSize;
    for (int cell = 0; cell < side * side * QcWorld::sectionHeight; ++cell) {
        const int x = cell % side;
        const int z = cell / side % side;
        const int y = cell / (side * side);
        if (isLoader(section.block(x, y, z))) {
            found.insert(corner.offset(x, y, z));
        }
    }
}

void QcChunkLoaders::add(QcBlockPos pos, const QString &owner)
{
    if (m_byPos.contains(pos)) {
        return;
    }
    const quint64 order = m_nextOrder++;
    ++m_changes;
    m_byPos.insert(pos, {order, owner});
    m_byOrder.emplace(order, pos);
    if (!owner.isEmpty()) {
        ++m_perOwner[owner];
    }
}

void QcChunkLoaders::remove(QcBlockPos pos)
{
    const auto it = m_byPos.constFind(pos);
    if (it == m_byPos.cend()) {
        return;
    }
    m_byOrder.erase(it->order);
    ++m_changes;
    if (!it->owner.isEmpty() && --m_perOwner[it->owner] <= 0) {
        m_perOwner.remove(it->owner);
    }
    m_byPos.erase(it);
}

void QcChunkLoaders::setOwner(QcBlockPos pos, const QString &owner)
{
    const auto it = m_byPos.find(pos);
    if (it == m_byPos.end() || it->owner == owner) {
        return;
    }
    if (!it->owner.isEmpty() && --m_perOwner[it->owner] <= 0) {
        m_perOwner.remove(it->owner);
    }
    it->owner = owner;
    if (!owner.isEmpty()) {
        ++m_perOwner[owner];
    }
}

QList<QcChunkLoaders::Loader> QcChunkLoaders::loaders() const
{
    QList<Loader> loaders;
    loaders.reserve(m_byOrder.size());
    for (const auto &[order, pos] : m_byOrder) {
        loaders.append({pos, m_byPos.value(pos).owner});
    }
    return loaders;
}

void QcChunkLoaders::restore(const QList<Loader> &loaders)
{
    m_byPos.clear();
    m_byOrder.clear();
    m_perOwner.clear();
    m_nextOrder = 0;
    ++m_changes;
    for (const Loader &loader : loaders) {
        add(loader.pos, loader.owner);
    }
}

QList<QcChunkPos> QcChunkLoaders::chunks(qsizetype limit) const
{
    QList<QcChunkPos> chunks;
    QSet<QcChunkPos> seen;
    qsizetype taken = 0;
    for (auto it = m_byOrder.cbegin(); it != m_byOrder.cend() && taken < limit; ++it, ++taken) {
        if (!seen.contains(it->second.chunk())) {
            seen.insert(it->second.chunk());
            chunks.append(it->second.chunk());
        }
    }
    return chunks;
}
