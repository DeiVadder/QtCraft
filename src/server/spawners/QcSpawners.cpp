// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/spawners/QcSpawners.h"

#include <algorithm>

QcSpawners::QcSpawners(const QcContentRegistry &content)
{
    const auto count = static_cast<std::size_t>(content.stateCount());
    m_spawner.resize(count);
    m_defaultMob.resize(count);
    for (std::size_t id = 0; id < count; ++id) {
        const QcSpawnerDefinition &spawner = content.block(static_cast<QcBlockId>(id)).spawner;
        m_spawner[id] = spawner.isSpawner;
        m_defaultMob[id] = spawner.mob;
    }
}

void QcSpawners::noteBlock(QcBlockPos pos, QcBlockId before, QcBlockId after)
{
    Q_UNUSED(before);
    if (!isSpawner(after)) {
        remove(pos);
        return;
    }
    if (!m_entries.contains(pos)) {
        add({pos, m_defaultMob[static_cast<std::size_t>(after)]});
    }
}

void QcSpawners::noteChunk(QcChunkPos pos, const QcChunk &chunk, quint32 sections)
{
    constexpr int size = QcWorld::chunkSize;
    for (int index = 0; index < QcWorld::sectionCount; ++index) {
        const QcChunkSection &section = chunk.section(index);
        if ((sections & (1u << static_cast<unsigned>(index))) == 0 || !std::ranges::any_of(section.palette(), [this](QcBlockId id) { return isSpawner(id); })) {
            continue;
        }

        const QcBlockPos corner{pos.x * size, index * QcWorld::sectionHeight, pos.z * size};
        for (int cell = 0; cell < size * size * QcWorld::sectionHeight; ++cell) {
            const int x = cell % size;
            const int z = cell / size % size;
            const int y = cell / (size * size);
            const QcBlockId block = section.block(x, y, z);
            if (isSpawner(block) && !m_entries.contains(corner.offset(x, y, z))) {
                add({corner.offset(x, y, z), m_defaultMob[static_cast<std::size_t>(block)]});
            }
        }
    }
}

void QcSpawners::forgetChunk(QcChunkPos pos)
{
    const QList<QcBlockPos> positions = m_byChunk.take(pos);
    for (const QcBlockPos &at : positions) {
        m_entries.remove(at);
    }
}

QList<QcSpawners::Entry> QcSpawners::savedOf(QcChunkPos pos) const
{
    QList<QcBlockPos> positions = m_byChunk.value(pos);
    std::ranges::sort(positions);
    QList<Entry> entries;
    entries.reserve(positions.size());
    for (const QcBlockPos &at : std::as_const(positions)) {
        entries.append(m_entries.value(at));
    }
    return entries;
}

QList<QcSpawners::Entry> QcSpawners::takeChunk(QcChunkPos pos)
{
    QList<Entry> entries = savedOf(pos);
    forgetChunk(pos);
    return entries;
}

void QcSpawners::restore(const QList<Entry> &entries, const QcChunk &chunk)
{
    for (const Entry &entry : entries) {
        if (entry.pos.chunk() == chunk.pos() && isSpawner(chunk.block(entry.pos.localX(), entry.pos.y, entry.pos.localZ()))) {
            set(entry);
        }
    }
}

bool QcSpawners::set(const Entry &entry)
{
    const auto it = m_entries.find(entry.pos);
    if (it == m_entries.end()) {
        return false;
    }
    // Clients see only the kind.
    if (it->mob != entry.mob) {
        m_changes.append(entry.pos);
    }
    *it = entry;
    it->delay = std::min(it->delay, maxDelay);
    return true;
}

const QcSpawners::Entry *QcSpawners::at(QcBlockPos pos) const
{
    const auto it = m_entries.constFind(pos);
    return it != m_entries.cend() ? &*it : nullptr;
}

QcSpawners::Entry *QcSpawners::at(QcBlockPos pos)
{
    const auto it = m_entries.find(pos);
    return it != m_entries.end() ? &*it : nullptr;
}

QList<QcBlockPos> QcSpawners::positions() const
{
    QList<QcBlockPos> positions = m_entries.keys();
    std::ranges::sort(positions);
    return positions;
}

QList<QcBlockPos> QcSpawners::takeChanges()
{
    QList<QcBlockPos> changes = std::move(m_changes);
    m_changes.clear();
    std::ranges::sort(changes);
    const auto [first, last] = std::ranges::unique(changes);
    changes.erase(first, last);
    return changes;
}

void QcSpawners::clear()
{
    m_entries.clear();
    m_byChunk.clear();
    m_changes.clear();
}

void QcSpawners::add(const Entry &entry)
{
    m_entries.insert(entry.pos, entry);
    m_byChunk[entry.pos.chunk()].append(entry.pos);
    m_changes.append(entry.pos);
}

void QcSpawners::remove(QcBlockPos pos)
{
    if (!m_entries.remove(pos)) {
        return;
    }
    const auto chunk = m_byChunk.find(pos.chunk());
    if (chunk != m_byChunk.end()) {
        chunk->removeOne(pos);
        if (chunk->isEmpty()) {
            m_byChunk.erase(chunk);
        }
    }
    m_changes.append(pos);
}
