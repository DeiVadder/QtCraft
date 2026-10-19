#include "server/chunks/QcChunkLinks.h"

#include "server/signal/QcSignalTable.h"

#include <algorithm>

namespace {

// Bits of the marks: strips in bits 0-3, reach in bits 4-7, by side.
constexpr int reachShift = 4;

int sideOfFace(QcBlockFace face)
{
    switch (face) {
    case QcBlockFace::East:
        return 0;
    case QcBlockFace::West:
        return 1;
    case QcBlockFace::South:
        return 2;
    case QcBlockFace::North:
        return 3;
    case QcBlockFace::Up:
    case QcBlockFace::Down:
        break;
    }
    return -1;
}

// Cells from local `x`, `z` to the last cell before `side`.
int distanceTo(int side, int x, int z)
{
    constexpr int last = QcWorld::chunkSize - 1;
    switch (side) {
    case 0:
        return last - x;
    case 1:
        return x;
    case 2:
        return last - z;
    default:
        return z;
    }
}

} // namespace

QcChunkLinks::QcChunkLinks(const QcContentRegistry &content)
{
    const QcSignalTable table(content);
    const auto stateCount = std::size_t(content.stateCount());
    m_part.resize(stateCount);
    m_piston.resize(stateCount, 0);
    for (std::size_t id = 0; id < stateCount; ++id) {
        const QcSignalState &state = table.at(QcBlockId(id));
        m_part[id] = content.block(QcBlockId(id)).signal.isCircuitPart();
        if (state.kind == QcSignalKind::Piston && state.facing) {
            m_piston[id] = quint8(sideOfFace(*state.facing) + 1);
        }
    }
}

QcChunkPos QcChunkLinks::neighbour(QcChunkPos chunk, int side)
{
    switch (side) {
    case 0:
        return {chunk.x + 1, chunk.z};
    case 1:
        return {chunk.x - 1, chunk.z};
    case 2:
        return {chunk.x, chunk.z + 1};
    default:
        return {chunk.x, chunk.z - 1};
    }
}

bool QcChunkLinks::linked(quint16 marks, quint16 neighbourMarks, int side)
{
    const int back = opposite(side);
    const bool strips = (marks & (1u << side)) != 0 && (neighbourMarks & (1u << back)) != 0;
    const bool reaches = (marks & (1u << (side + reachShift))) != 0 || (neighbourMarks & (1u << (back + reachShift))) != 0;
    return strips || reaches;
}

bool QcChunkLinks::Counts::isEmpty() const
{
    for (int side = 0; side < sideCount; ++side) {
        if (strip[std::size_t(side)] != 0 || reach[std::size_t(side)] != 0) {
            return false;
        }
    }
    return true;
}

void QcChunkLinks::Counts::add(const QcChunkLinks &links, int x, int z, QcBlockId block, int delta)
{
    const auto id = std::size_t(block);
    if (id >= links.m_part.size()) {
        return;
    }
    if (links.m_part[id]) {
        for (int side = 0; side < sideCount; ++side) {
            if (distanceTo(side, x, z) < stripWidth) {
                strip[std::size_t(side)] += delta;
            }
        }
    }
    if (const int side = int(static_cast<unsigned char>(links.m_piston[id])) - 1; side >= 0 && distanceTo(side, x, z) < pistonReach) {
        reach[std::size_t(side)] += delta;
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
    return it == m_counts.cend() ? quint16(0) : marksOf(*it);
}

quint16 QcChunkLinks::marksOf(const Counts &counts)
{
    quint16 marks = 0;
    for (int side = 0; side < sideCount; ++side) {
        if (counts.strip[std::size_t(side)] > 0) {
            marks |= quint16(1u << side);
        }
        if (counts.reach[std::size_t(side)] > 0) {
            marks |= quint16(1u << (side + reachShift));
        }
    }
    return marks;
}
