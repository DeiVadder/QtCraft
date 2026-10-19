#include "server/storage/QcRegionFile.h"

#include "common/core/QcCrc32.h"
#include "common/core/QcIntMath.h"
#include "common/core/QcWorldConstants.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

constexpr QDataStream::Version streamVersion = QDataStream::Qt_6_8;
constexpr qint64 headerBytes = 4 + 2 + 4 + 4;
constexpr qint64 entryBytes = 2 + 2 + 4 + 4 + 4;
constexpr qint64 maxFileBytes = qint64(1024) * 1024 * 1024;

QcChunkPos chunkOfSlot(QcChunkPos region, int slot)
{
    return {region.x * QcRegionFile::regionSize + slot % QcRegionFile::regionSize,
            region.z * QcRegionFile::regionSize + slot / QcRegionFile::regionSize};
}

int slotOf(QcChunkPos chunk)
{
    return qcFloorMod(chunk.x, QcRegionFile::regionSize) + QcRegionFile::regionSize * qcFloorMod(chunk.z, QcRegionFile::regionSize);
}

} // namespace

namespace QcRegionFile {

QcChunkPos regionOf(QcChunkPos chunk)
{
    return {qcFloorDiv(chunk.x, regionSize), qcFloorDiv(chunk.z, regionSize)};
}

QString path(const QString &folder, QcChunkPos region, const QString &extension)
{
    return QDir(folder).filePath(u"r.%1.%2.%3"_s.arg(region.x).arg(region.z).arg(extension));
}

[[nodiscard]] std::optional<QcChunkPos> regionOfFile(const QString &fileName, const QString &extension)
{
    const QStringList parts = fileName.split(u'.');
    bool xOk = false;
    bool zOk = false;
    const QcChunkPos region{parts.value(1).toInt(&xOk), parts.value(2).toInt(&zOk)};
    if (parts.size() != 4 || parts[0] != u"r"_s || parts[3] != extension || !xOk || !zOk) {
        return std::nullopt;
    }
    const qint64 limit = QcWorld::maxHorizontalChunk / regionSize + 1;
    if (std::abs(qint64(region.x)) > limit || std::abs(qint64(region.z)) > limit) {
        return std::nullopt;
    }
    return region;
}

[[nodiscard]] QcResult<Index> readIndex(const QString &path, QcChunkPos region, QByteArrayView magic, quint16 version)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return qcError(u"Cannot open %1: %2"_s.arg(path, file.errorString()));
    }
    const qint64 fileSize = file.size();
    const QByteArray header = file.read(headerBytes);
    if (header.size() != headerBytes || !header.startsWith(magic) || fileSize > maxFileBytes) {
        return qcError(u"%1 is not a QtCraft region file or is damaged"_s.arg(path));
    }

    QDataStream stream(header);
    stream.setVersion(streamVersion);
    stream.skipRawData(4);
    quint16 fileVersion = 0;
    quint32 count = 0;
    quint32 crc = 0;
    stream >> fileVersion >> count >> crc;
    if (fileVersion != version) {
        return qcError(fileVersion < version ? u"%1 is from an older development version"_s.arg(path)
                                             : u"%1 has unsupported format version %2"_s.arg(path).arg(fileVersion));
    }
    if (count > quint32(chunksPerRegion)) {
        return qcError(u"%1 is damaged (chunk count)"_s.arg(path));
    }

    const QByteArray entries = file.read(qint64(count) * entryBytes);
    if (entries.size() != qint64(count) * entryBytes || qcCrc32(entries) != crc) {
        return qcError(u"%1 is damaged (index)"_s.arg(path));
    }

    Index index;
    QDataStream table(entries);
    table.setVersion(streamVersion);
    const qint64 dataStart = headerBytes + qint64(count) * entryBytes;
    for (quint32 i = 0; i < count; ++i) {
        quint16 slot = 0;
        Entry entry;
        table >> slot >> entry.marks >> entry.offset >> entry.size >> entry.crc;
        const QcChunkPos chunk = chunkOfSlot(region, slot);
        const bool inWorld = std::abs(qint64(chunk.x)) <= QcWorld::maxHorizontalChunk && std::abs(qint64(chunk.z)) <= QcWorld::maxHorizontalChunk;
        if (int(slot) >= chunksPerRegion || !inWorld || index.contains(chunk) || std::cmp_less(entry.offset, dataStart) || entry.size > maxBlobBytes
            || qint64(entry.offset) + qint64(entry.size) > fileSize) {
            return qcError(u"%1 is damaged (entry %2)"_s.arg(path).arg(i));
        }
        index.insert(chunk, entry);
    }
    return index;
}

[[nodiscard]] QcResult<QByteArray> readBlob(const QString &path, Entry entry)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || !file.seek(entry.offset)) {
        return qcError(u"Cannot read %1: %2"_s.arg(path, file.errorString()));
    }
    QByteArray data = file.read(entry.size);
    if (data.size() != qsizetype(entry.size) || qcCrc32(data) != entry.crc) {
        return qcError(u"%1 is damaged (chunk data)"_s.arg(path));
    }
    return data;
}

[[nodiscard]] QcResult<AllBlobs> readAll(const QString &path, const Index &index)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maxFileBytes) {
        return qcError(u"Cannot read %1: %2"_s.arg(path, file.errorString()));
    }
    const QByteArray all = file.readAll();
    AllBlobs result;
    result.blobs.reserve(index.size());
    for (auto it = index.cbegin(); it != index.cend(); ++it) {
        const Entry &entry = it.value();
        if (qint64(entry.offset) + qint64(entry.size) > all.size()) {
            return qcError(u"%1 is damaged (chunk data)"_s.arg(path));
        }
        QByteArray data = all.mid(entry.offset, entry.size);
        if (qcCrc32(data) != entry.crc) {
            result.damaged.append(it.key());
        }
        result.blobs.append({it.key(), entry.marks, std::move(data), entry.crc});
    }
    return result;
}

[[nodiscard]] QcResult<Index> write(const QString &path, QByteArrayView magic, quint16 version, const QList<Blob> &blobs)
{
    // In slot order, so the same chunks give the same file.
    QList<const Blob *> sorted;
    sorted.reserve(blobs.size());
    for (const Blob &blob : blobs) {
        sorted.append(&blob);
    }
    std::ranges::sort(sorted, {}, [](const Blob *blob) { return slotOf(blob->chunk); });

    Index index;
    QByteArray entries;
    QDataStream table(&entries, QIODevice::WriteOnly);
    table.setVersion(streamVersion);
    qint64 offset = headerBytes + qint64(sorted.size()) * entryBytes;
    for (const Blob *blob : std::as_const(sorted)) {
        const Entry entry{blob->marks, quint32(offset), quint32(blob->data.size()), blob->crc.value_or(qcCrc32(blob->data))};
        table << quint16(slotOf(blob->chunk)) << entry.marks << entry.offset << entry.size << entry.crc;
        index.insert(blob->chunk, entry);
        offset += blob->data.size();
    }
    if (offset > maxFileBytes) {
        return qcError(u"%1 would be too large"_s.arg(path));
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return qcError(u"Cannot write %1: %2"_s.arg(path, file.errorString()));
    }
    QDataStream stream(&file);
    stream.setVersion(streamVersion);
    stream.writeRawData(magic.data(), int(magic.size()));
    stream << version << quint32(sorted.size()) << qcCrc32(entries);
    stream.writeRawData(entries.constData(), int(entries.size()));
    for (const Blob *blob : std::as_const(sorted)) {
        stream.writeRawData(blob->data.constData(), int(blob->data.size()));
    }
    if (stream.status() != QDataStream::Ok || !file.commit()) {
        return qcError(u"Cannot write %1: %2"_s.arg(path, file.errorString()));
    }
    return index;
}

} // namespace QcRegionFile
