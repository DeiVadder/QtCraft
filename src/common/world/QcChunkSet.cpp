// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/world/QcChunkSet.h"

#include "common/core/QcIntMath.h"
#include "common/core/QcWorldConstants.h"
#include "common/protocol/QcStreamLimits.h"

#include <QDataStream>
#include <QIODevice>

#include <algorithm>
#include <bit>

using namespace Qt::StringLiterals;

namespace {

constexpr int regionSize = QcChunkSet::regionSize;
// The regions within the world (decision E-883); bytes naming others are forged or damaged, and
// their chunk coordinates would overflow.
constexpr int lowestRegion = qcFloorDiv(-QcWorld::maxHorizontalChunk, regionSize);
constexpr int highestRegion = qcFloorDiv(QcWorld::maxHorizontalChunk, regionSize);
constexpr qsizetype regionsInWorld = qsizetype{highestRegion - lowestRegion + 1} * (highestRegion - lowestRegion + 1);
// Every region at most once: more is damage (the limit only bounds what a count may promise).
constexpr quint32 maxRegions = static_cast<quint32>(std::min<qsizetype>(regionsInWorld, qsizetype{1} << 24));

// The slot of `chunk` within its region `region`.
int slotOf(QcChunkPos chunk, QcChunkPos region)
{
    return (chunk.x - region.x * regionSize) + regionSize * (chunk.z - region.z * regionSize);
}

bool hasSlot(const QByteArray &bitmap, int slot)
{
    return (static_cast<quint8>(bitmap.at(slot / 8)) & (1u << (slot % 8))) != 0;
}

bool inWorld(qint32 x, qint32 z)
{
    return x >= lowestRegion && x <= highestRegion && z >= lowestRegion && z <= highestRegion;
}

// Whether every chunk of `bitmap`, the bitmap of `region`, lies inside the world: the regions at
// its edge reach a little beyond it (the world is not a whole number of regions wide).
bool insideWorld(QcChunkPos region, const QByteArray &bitmap)
{
    const bool edge = region.x == lowestRegion || region.x == highestRegion || region.z == lowestRegion || region.z == highestRegion;
    if (!edge) {
        return true;
    }
    for (int slot = 0; slot < regionSize * regionSize; ++slot) {
        const QcChunkPos chunk{region.x * regionSize + slot % regionSize, region.z * regionSize + slot / regionSize};
        if (hasSlot(bitmap, slot) && !chunk.isInsideWorld()) {
            return false;
        }
    }
    return true;
}

bool isBlank(const QByteArray &bitmap)
{
    return std::ranges::all_of(bitmap, [](char bits) { return bits == '\0'; });
}

} // namespace

QcChunkPos QcChunkSet::regionOf(QcChunkPos chunk)
{
    return {qcFloorDiv(chunk.x, regionSize), qcFloorDiv(chunk.z, regionSize)};
}

bool QcChunkSet::insert(QcChunkPos chunk)
{
    Q_ASSERT(chunk.isInsideWorld());
    const QcChunkPos region = regionOf(chunk);
    auto it = m_regions.find(region);
    if (it == m_regions.end()) {
        it = m_regions.insert(region, QByteArray(bitmapBytes, '\0'));
    }
    const int slot = slotOf(chunk, region);
    if (hasSlot(*it, slot)) {
        return false;
    }
    char &bits = (*it)[slot / 8];
    bits = static_cast<char>(static_cast<quint8>(bits) | (1u << (slot % 8)));
    return true;
}

bool QcChunkSet::contains(QcChunkPos chunk) const
{
    const QcChunkPos region = regionOf(chunk);
    const auto it = m_regions.constFind(region);
    return it != m_regions.cend() && hasSlot(*it, slotOf(chunk, region));
}

qsizetype QcChunkSet::size() const
{
    qsizetype count = 0;
    for (const QByteArray &bitmap : m_regions) {
        for (const char bits : bitmap) {
            count += std::popcount(static_cast<quint8>(bits));
        }
    }
    return count;
}

QList<QcChunkPos> QcChunkSet::regions() const
{
    QList<QcChunkPos> regions = m_regions.keys();
    std::ranges::sort(regions, {}, [](QcChunkPos region) { return std::pair{region.z, region.x}; });
    return regions;
}

bool QcChunkSet::hasRegionWithin(QcChunkPos lowest, QcChunkPos highest) const
{
    for (auto it = m_regions.cbegin(); it != m_regions.cend(); ++it) {
        const QcChunkPos region = it.key();
        if (region.x >= lowest.x && region.x <= highest.x && region.z >= lowest.z && region.z <= highest.z) {
            return true;
        }
    }
    return false;
}

QSet<QcChunkPos> QcChunkSet::chunks() const
{
    QSet<QcChunkPos> all;
    for (auto it = m_regions.cbegin(); it != m_regions.cend(); ++it) {
        for (int slot = 0; slot < regionSize * regionSize; ++slot) {
            if (hasSlot(*it, slot)) {
                all.insert({it.key().x * regionSize + slot % regionSize, it.key().z * regionSize + slot / regionSize});
            }
        }
    }
    return all;
}

void QcChunkSet::unite(const QcChunkSet &other)
{
    for (auto it = other.m_regions.cbegin(); it != other.m_regions.cend(); ++it) {
        auto mine = m_regions.find(it.key());
        if (mine == m_regions.end()) {
            m_regions.insert(it.key(), *it);
            continue;
        }
        for (int i = 0; i < bitmapBytes; ++i) {
            (*mine)[i] = static_cast<char>(static_cast<quint8>(mine->at(i)) | static_cast<quint8>(it->at(i)));
        }
    }
}

QcChunkSet QcChunkSet::without(const QcChunkSet &other) const
{
    QcChunkSet rest;
    for (auto it = m_regions.cbegin(); it != m_regions.cend(); ++it) {
        QByteArray bitmap = *it;
        if (const auto theirs = other.m_regions.constFind(it.key()); theirs != other.m_regions.cend()) {
            for (int i = 0; i < bitmapBytes; ++i) {
                bitmap[i] = static_cast<char>(static_cast<quint8>(bitmap.at(i)) & ~static_cast<quint8>(theirs->at(i)));
            }
        }
        if (!isBlank(bitmap)) {
            rest.m_regions.insert(it.key(), bitmap);
        }
    }
    return rest;
}

bool QcChunkSet::intersects(const QcChunkSet &other) const
{
    for (auto it = m_regions.cbegin(); it != m_regions.cend(); ++it) {
        const auto theirs = other.m_regions.constFind(it.key());
        if (theirs == other.m_regions.cend()) {
            continue;
        }
        for (int i = 0; i < bitmapBytes; ++i) {
            if ((static_cast<quint8>(it->at(i)) & static_cast<quint8>(theirs->at(i))) != 0) {
                return true;
            }
        }
    }
    return false;
}

QByteArray QcChunkSet::bytes() const
{
    const QList<QcChunkPos> sorted = regions();
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QcChunkSet::streamVersion);
    stream << static_cast<quint32>(sorted.size());
    for (const QcChunkPos &region : sorted) {
        stream << static_cast<qint32>(region.x) << static_cast<qint32>(region.z);
        stream.writeRawData(m_regions.value(region).constData(), bitmapBytes);
    }
    return payload;
}

QcResult<QcChunkSet> QcChunkSet::parse(QByteArrayView bytes)
{
    const QByteArray copy = bytes.toByteArray();
    QDataStream stream(copy);
    stream.setVersion(QcChunkSet::streamVersion);
    const std::optional<quint32> count = QcStreamLimits::readCount(stream, maxRegions);
    if (!count) {
        return qcError(u"cut short"_s);
    }

    QcChunkSet set;
    for (quint32 i = 0; i < *count; ++i) {
        qint32 x = 0;
        qint32 z = 0;
        QByteArray bitmap(bitmapBytes, '\0');
        stream >> x >> z;
        if (stream.readRawData(bitmap.data(), bitmapBytes) != bitmapBytes || stream.status() != QDataStream::Ok) {
            return qcError(u"cut short"_s);
        }
        if (!inWorld(x, z) || set.m_regions.contains({x, z}) || !insideWorld({x, z}, bitmap)) {
            return qcError(u"region %1, %2"_s.arg(x).arg(z));
        }
        // A region without chunks is never written; keeping none keeps operator== exact.
        if (!isBlank(bitmap)) {
            set.m_regions.insert({x, z}, bitmap);
        }
    }
    if (!stream.atEnd()) {
        return qcError(u"trailing bytes"_s);
    }
    return set;
}
