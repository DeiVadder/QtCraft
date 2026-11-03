// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/chunks/QcChunkLinks.h"

#include "server/signal/QcSignalTable.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace {

// Bits of the marks: strips in bits 0-3, reach in bits 4-7, by side.
constexpr std::size_t reachShift = 4;

using Side = QcChunkLinks::Side;

std::optional<Side> sideOfFace(QcBlockFace face)
{
    switch (face) {
    case QcBlockFace::East:
        return Side::East;
    case QcBlockFace::West:
        return Side::West;
    case QcBlockFace::South:
        return Side::South;
    case QcBlockFace::North:
        return Side::North;
    case QcBlockFace::Up:
    case QcBlockFace::Down:
        break;
    }
    return std::nullopt;
}

// The index of `side` in arrays by side, and its bit in the marks.
constexpr std::size_t slotOf(Side side)
{
    return std::to_underlying(side);
}

// Cells from local `x`, `z` to the last cell before `side`.
int distanceTo(Side side, int x, int z)
{
    constexpr int last = QcWorld::chunkSize - 1;
    switch (side) {
    case Side::East:
        return last - x;
    case Side::West:
        return x;
    case Side::South:
        return last - z;
    case Side::North:
        break;
    }
    return z;
}

} // namespace

QcChunkLinks::QcChunkLinks(const QcContentRegistry &content)
{
    const QcSignalTable table(content);
    const auto stateCount = static_cast<std::size_t>(content.stateCount());
    m_part.resize(stateCount);
    m_piston.resize(stateCount, 0);
    for (std::size_t id = 0; id < stateCount; ++id) {
        const QcSignalState &state = table.at(static_cast<QcBlockId>(id));
        m_part[id] = content.block(static_cast<QcBlockId>(id)).signal.isCircuitPart();
        const std::optional<Side> side = state.kind == QcSignalKind::Piston && state.facing ? sideOfFace(*state.facing) : std::nullopt;
        if (side) {
            m_piston[id] = static_cast<quint8>(std::to_underlying(*side) + 1);
        }
    }
}

QcChunkPos QcChunkLinks::neighbour(QcChunkPos chunk, Side side)
{
    switch (side) {
    case Side::East:
        return {chunk.x + 1, chunk.z};
    case Side::West:
        return {chunk.x - 1, chunk.z};
    case Side::South:
        return {chunk.x, chunk.z + 1};
    case Side::North:
        break;
    }
    return {chunk.x, chunk.z - 1};
}

bool QcChunkLinks::linked(quint16 marks, quint16 neighbourMarks, Side side)
{
    const std::size_t here = slotOf(side);
    const std::size_t back = slotOf(opposite(side));
    const bool strips = (marks & (1u << here)) != 0 && (neighbourMarks & (1u << back)) != 0;
    const bool reaches = (marks & (1u << (here + reachShift))) != 0 || (neighbourMarks & (1u << (back + reachShift))) != 0;
    return strips || reaches;
}

bool QcChunkLinks::Counts::isEmpty() const
{
    return std::ranges::all_of(strip, [](int count) { return count == 0; }) && std::ranges::all_of(reach, [](int count) { return count == 0; });
}

void QcChunkLinks::Counts::add(const QcChunkLinks &links, int x, int z, QcBlockId block, int delta)
{
    const auto id = static_cast<std::size_t>(block);
    if (id >= links.m_part.size()) {
        return;
    }
    if (links.m_part[id]) {
        for (const Side side : sides) {
            if (distanceTo(side, x, z) < stripWidth) {
                strip[slotOf(side)] += delta;
            }
        }
    }
    if (const quint8 piston = links.m_piston[id]; piston != 0) {
        const auto side = static_cast<Side>(piston - 1);
        if (distanceTo(side, x, z) < pistonReach) {
            reach[slotOf(side)] += delta;
        }
    }
}

quint16 QcChunkLinks::marksOf(const QcChunk &chunk) const
{
    return marksOf(countChunk(chunk));
}

QcChunkLinks::Counts QcChunkLinks::countChunk(const QcChunk &chunk) const
{
    Counts counts;
    for (int s = 0; s < QcWorld::sectionCount; ++s) {
        const QcChunkSection &section = chunk.section(s);
        if (std::ranges::any_of(section.palette(), [this](QcBlockId id) { return matters(id); })) {
            counts.addSection(*this, section);
        }
    }
    return counts;
}

void QcChunkLinks::Counts::addSection(const QcChunkLinks &links, const QcChunkSection &section)
{
    for (int y = 0; y < QcWorld::sectionHeight; ++y) {
        for (int z = 0; z < QcWorld::chunkSize; ++z) {
            for (int x = 0; x < QcWorld::chunkSize; ++x) {
                add(links, x, z, section.block(x, y, z), 1);
            }
        }
    }
}

void QcChunkLinks::noteChunk(const QcChunk &chunk)
{
    ++m_changes;
    const Counts counts = countChunk(chunk);
    if (counts.isEmpty()) {
        m_counts.remove(chunk.pos());
    } else {
        m_counts.insert(chunk.pos(), counts);
    }
}

void QcChunkLinks::noteBlock(QcBlockPos pos, QcBlockId before, QcBlockId after)
{
    if (!matters(before) && !matters(after)) {
        return;
    }
    Counts &counts = m_counts[pos.chunk()];
    const quint16 marks = marksOf(counts);
    counts.add(*this, pos.localX(), pos.localZ(), before, -1);
    counts.add(*this, pos.localX(), pos.localZ(), after, 1);
    if (marksOf(counts) != marks) {
        ++m_changes;
    }
    if (counts.isEmpty()) {
        m_counts.remove(pos.chunk());
    }
}

void QcChunkLinks::forgetChunk(QcChunkPos chunk)
{
    ++m_changes;
    m_counts.remove(chunk);
}

quint16 QcChunkLinks::marksOf(QcChunkPos chunk) const
{
    const auto it = m_counts.constFind(chunk);
    return it == m_counts.cend() ? quint16{0} : marksOf(*it);
}

quint16 QcChunkLinks::marksOf(const Counts &counts)
{
    quint16 marks = 0;
    for (const Side side : sides) {
        if (counts.strip[slotOf(side)] > 0) {
            marks |= static_cast<quint16>(1u << slotOf(side));
        }
        if (counts.reach[slotOf(side)] > 0) {
            marks |= static_cast<quint16>(1u << (slotOf(side) + reachShift));
        }
    }
    return marks;
}
