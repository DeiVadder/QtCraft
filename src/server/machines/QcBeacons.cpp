// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/machines/QcBeacons.h"

#include "common/content/registry/QcContentRegistry.h"

#include <algorithm>

QcBeacons::QcBeacons(const QcContentRegistry &content)
    : m_beacon(static_cast<std::size_t>(content.stateCount()))
{
    for (std::size_t id = 0; id < m_beacon.size(); ++id) {
        m_beacon[id] = content.block(static_cast<QcBlockId>(id)).opensBeacon;
    }
}

void QcBeacons::noteChunk(QcChunkPos pos, const QcChunk &chunk)
{
    forgetChunk(pos);
    for (int index = 0; index < QcWorld::sectionCount; ++index) {
        const QcChunkSection &section = chunk.section(index);
        if (std::ranges::none_of(section.palette(), [this](QcBlockId id) { return isBeacon(id); })) {
            continue;
        }
        const QcBlockPos corner{pos.x * QcWorld::chunkSize, index * QcWorld::sectionHeight, pos.z * QcWorld::chunkSize};
        section.forEachCell(corner, [this](QcBlockId id) { return isBeacon(id); }, [this](QcBlockPos block) { add(block); });
    }
}

void QcBeacons::noteBlock(QcBlockPos pos, QcBlockId block)
{
    if (isBeacon(block)) {
        add(pos);
    } else {
        remove(pos);
    }
}

void QcBeacons::forgetChunk(QcChunkPos pos)
{
    const QList<QcBlockPos> positions = m_byChunk.take(pos);
    for (const QcBlockPos &beacon : positions) {
        m_entries.remove(beacon);
    }
}

QList<QcBeacons::Saved> QcBeacons::savedOf(QcChunkPos pos) const
{
    QList<QcBlockPos> positions = m_byChunk.value(pos);
    std::ranges::sort(positions);
    QList<Saved> saved;
    for (const QcBlockPos &beacon : std::as_const(positions)) {
        const Entry *entry = at(beacon);
        if (entry && (!entry->primary.isEmpty() || !entry->secondary.isEmpty())) {
            saved.append({entry->pos, entry->primary, entry->secondary});
        }
    }
    return saved;
}

void QcBeacons::restore(const QList<Saved> &beacons)
{
    for (const Saved &saved : beacons) {
        if (Entry *entry = at(saved.pos)) {
            entry->primary = saved.primary;
            entry->secondary = saved.secondary;
        }
    }
}

QcBeacons::Entry *QcBeacons::at(QcBlockPos pos)
{
    const auto it = m_entries.find(pos);
    return it == m_entries.end() ? nullptr : &*it;
}

const QcBeacons::Entry *QcBeacons::at(QcBlockPos pos) const
{
    const auto it = m_entries.constFind(pos);
    return it == m_entries.constEnd() ? nullptr : &*it;
}

QList<QcBlockPos> QcBeacons::positions() const
{
    QList<QcBlockPos> positions = m_entries.keys();
    std::ranges::sort(positions);
    return positions;
}

void QcBeacons::add(QcBlockPos pos)
{
    if (m_entries.contains(pos)) {
        return;
    }
    m_entries.insert(pos, Entry{pos, {}, {}, 0, {}});
    m_byChunk[pos.chunk()].append(pos);
}

void QcBeacons::remove(QcBlockPos pos)
{
    const auto entry = m_entries.constFind(pos);
    if (entry == m_entries.constEnd()) {
        return;
    }
    if (entry->isActive()) {
        m_darkened.append(pos);
    }
    m_entries.erase(entry);
    const auto chunk = m_byChunk.find(pos.chunk());
    chunk->removeOne(pos);
    if (chunk->isEmpty()) {
        m_byChunk.erase(chunk);
    }
}

QcBeacons::Entry::Change QcBeacons::Entry::look(const QcContentRegistry &content, const QcBeacon::BlockAt &blockAt)
{
    const Change change{isActive(), level, false};
    const QList<QcBeamSection> found = QcBeacon::beamAt(content, pos, blockAt);
    // Like the reference, only a beacon with a beam looks at its pyramid again.
    if (!found.isEmpty()) {
        level = QcBeacon::levelAt(content, pos, blockAt);
    }
    const bool beamChanged = found != beam;
    beam = found;
    return {change.wasActive, change.levelBefore, beamChanged};
}
