// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/storage/QcGeneratedChunks.h"

#include "common/core/QcIntMath.h"
#include "common/core/QcWorldConstants.h"
#include "common/protocol/QcStreamLimits.h"
#include "server/storage/QcRegionFile.h"
#include "server/storage/QcSaveFile.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QList>

#include <algorithm>
#include <limits>

using namespace Qt::StringLiterals;

namespace {

constexpr int bitmapBytes = QcRegionFile::chunksPerRegion / 8;
constexpr int regionSize = QcRegionFile::regionSize;
// The regions within the world (decision E-883); a file naming others is forged or damaged, and
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

// Whether every name QcSaveFile::keepAside tries for `path` is taken.
bool asideCopiesTaken(const QString &path)
{
    for (int attempt = 1; attempt <= QcSaveFile::maxDamagedCopies; ++attempt) {
        if (!QFile::exists(attempt == 1 ? path + u".damaged"_s : u"%1.damaged.%2"_s.arg(path).arg(attempt))) {
            return false;
        }
    }
    return true;
}

// Keeps the damaged file `path` aside like the villages (QcServerPersistence::restoreVillages):
// the original goes only once its copy exists. Returns the warning for the player.
QString keptAside(const QString &path, const QString &error)
{
    const QString start = u"%1; the index of generated chunks starts empty"_s.arg(error);
    const QString copy = QcSaveFile::keepAside(path);
    if (copy.isEmpty()) {
        if (asideCopiesTaken(path)) {
            return u"%1, and all %2 copies kept aside (%3.damaged ...) are taken: the next save replaces the damaged file"_s.arg(
                start).arg(QcSaveFile::maxDamagedCopies).arg(QFileInfo(path).fileName());
        }
        return u"%1, and the file could not be kept aside: the next save replaces it"_s.arg(start);
    }
    QFile::remove(path);
    return u"%1, the damaged file was kept as %2"_s.arg(start, QFileInfo(copy).fileName());
}

} // namespace

void QcGeneratedChunks::note(QcChunkPos chunk)
{
    const QcChunkPos region = QcRegionFile::regionOf(chunk);
    auto it = m_regions.find(region);
    if (it == m_regions.end()) {
        it = m_regions.insert(region, QByteArray(bitmapBytes, '\0'));
    }
    const int slot = slotOf(chunk, region);
    if (hasSlot(*it, slot)) {
        return;
    }
    char &bits = (*it)[slot / 8];
    bits = static_cast<char>(static_cast<quint8>(bits) | (1u << (slot % 8)));
    setUnsaved(true);
}

bool QcGeneratedChunks::contains(QcChunkPos chunk) const
{
    const QcChunkPos region = QcRegionFile::regionOf(chunk);
    const auto it = m_regions.constFind(region);
    return it != m_regions.cend() && hasSlot(*it, slotOf(chunk, region));
}

QSet<QcChunkPos> QcGeneratedChunks::chunks() const
{
    QSet<QcChunkPos> all;
    for (auto it = m_regions.cbegin(); it != m_regions.cend(); ++it) {
        for (int slot = 0; slot < QcRegionFile::chunksPerRegion; ++slot) {
            if (hasSlot(*it, slot)) {
                all.insert({it.key().x * regionSize + slot % regionSize, it.key().z * regionSize + slot / regionSize});
            }
        }
    }
    return all;
}

QcGeneratedChunks::Opened QcGeneratedChunks::open(const QString &directory)
{
    Opened opened = load(directory, fileName.toString());
    if (!opened.newer) {
        return opened;
    }
    // A newer build's index stays as it is; what this build adds goes to a file of its own format
    // (decision E-946), which a newer build takes into its index.
    Opened older = load(directory, olderFileName.toString());
    older.newer = true;
    older.warnings = opened.warnings + older.warnings;
    return older;
}

QcGeneratedChunks::Opened QcGeneratedChunks::load(const QString &directory, const QString &name)
{
    Opened opened;
    opened.file = name;
    const QString path = QDir(directory).filePath(name);
    if (!QFile::exists(path)) {
        return opened;
    }
    // Any version passes the container check here, so a newer one is told apart from damage.
    const QcResult<QcSaveFile::Payload> payload = QcSaveFile::read(path, magic, std::numeric_limits<quint16>::max());
    if (payload && payload->version > formatVersion) {
        opened.newer = true;
        opened.warnings.append(u"%1 has format version %2, newer than this version of QtCraft reads (%3): it is left as it is, and the chunks "
                               u"generated now are kept in %4"_s.arg(path)
                                   .arg(payload->version)
                                   .arg(formatVersion)
                                   .arg(olderFileName));
        return opened;
    }
    const QcResult<QcGeneratedChunks> read = payload ? parse(payload->bytes, path) : QcResult<QcGeneratedChunks>(qcError(payload.error()));
    if (!read) {
        opened.warnings.append(keptAside(path, read.error()));
        return opened;
    }
    opened.index = *read;
    return opened;
}

QByteArray QcGeneratedChunks::bytes() const
{
    QList<QcChunkPos> regions = m_regions.keys();
    std::ranges::sort(regions, {}, [](QcChunkPos region) { return std::pair{region.z, region.x}; });

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QcSaveFile::streamVersion);
    stream << static_cast<quint32>(regions.size());
    for (const QcChunkPos &region : std::as_const(regions)) {
        stream << static_cast<qint32>(region.x) << static_cast<qint32>(region.z);
        stream.writeRawData(m_regions.value(region).constData(), bitmapBytes);
    }
    return QcSaveFile::bytes(magic, payload, formatVersion);
}

QcResult<QcGeneratedChunks> QcGeneratedChunks::parse(const QByteArray &payload, const QString &path)
{
    QDataStream stream(payload);
    stream.setVersion(QcSaveFile::streamVersion);
    const std::optional<quint32> count = QcStreamLimits::readCount(stream, maxRegions);
    if (!count) {
        return qcError(u"%1 is damaged"_s.arg(path));
    }

    QcGeneratedChunks index;
    for (quint32 i = 0; i < *count; ++i) {
        qint32 x = 0;
        qint32 z = 0;
        QByteArray bitmap(bitmapBytes, '\0');
        stream >> x >> z;
        if (stream.readRawData(bitmap.data(), bitmapBytes) != bitmapBytes || stream.status() != QDataStream::Ok) {
            return qcError(u"%1 is damaged"_s.arg(path));
        }
        if (!inWorld(x, z) || index.m_regions.contains({x, z})) {
            return qcError(u"%1 is damaged (region %2, %3)"_s.arg(path).arg(x).arg(z));
        }
        index.m_regions.insert({x, z}, bitmap);
    }
    if (!stream.atEnd()) {
        return qcError(u"%1 is damaged"_s.arg(path));
    }
    return index;
}
