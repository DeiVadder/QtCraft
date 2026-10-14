#include "QcRegionStorage.h"

#include "QcProtocol.h"
#include "QcSaveFile.h"
#include "QcStreamLimits.h"

#include <QDir>
#include <QFileInfo>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// Version 2 stores block states in the palette ("name[property=value,...]"); files of older
// development versions are refused (decision E-356).
constexpr quint16 regionVersion = 2;
constexpr QByteArrayView regionMagic("QCRG");

// Maps the file-local palette indices of every section to runtime ids.
QcStatus toRuntimeIds(QcChunk &chunk, const QList<QcBlockId> &runtimeIds, const QString &path)
{
    for (int s = 0; s < QcWorld::sectionCount; ++s) {
        QcChunkSection &section = chunk.section(s);
        QList<QcBlockId> palette;
        for (const QcBlockId local : section.palette()) {
            if (qsizetype(local) >= runtimeIds.size()) {
                return qcError(u"%1 is damaged (block index)"_s.arg(path));
            }
            palette.append(runtimeIds[qsizetype(local)]);
        }
        if (!section.assign(palette, section.indices())) {
            return qcError(u"%1 is damaged (section)"_s.arg(path));
        }
        section.compact();
    }
    return {};
}

} // namespace

QcRegionStorage::QcRegionStorage(QString directory, std::shared_ptr<const QcContentRegistry> content)
    : m_directory(std::move(directory))
    , m_content(std::move(content))
{
}

QcChunkPos QcRegionStorage::regionOf(QcChunkPos chunk)
{
    return {qcFloorDiv(chunk.x, regionSize), qcFloorDiv(chunk.z, regionSize)};
}

QString QcRegionStorage::regionPath(QcChunkPos region) const
{
    return QDir(m_directory).filePath(u"regions/r.%1.%2.qcr"_s.arg(region.x).arg(region.z));
}

QcStatus QcRegionStorage::write(const QHash<QcChunkPos, std::shared_ptr<const QcChunk>> &chunks) const
{
    QHash<QcChunkPos, QList<std::shared_ptr<const QcChunk>>> regions;
    for (auto it = chunks.constBegin(); it != chunks.constEnd(); ++it) {
        regions[regionOf(it.key())].append(it.value());
    }
    if (!regions.isEmpty() && !QDir().mkpath(QDir(m_directory).filePath(u"regions"_s))) {
        return qcError(u"Cannot create the regions directory in %1"_s.arg(m_directory));
    }

    for (auto region = regions.constBegin(); region != regions.constEnd(); ++region) {
        if (const QcStatus written = writeRegion(region.key(), region.value()); !written) {
            return written;
        }
    }
    return {};
}

QcStatus QcRegionStorage::writeRegion(QcChunkPos region, const QList<std::shared_ptr<const QcChunk>> &chunks) const
{
    // File-local palette of block names; chunk data refers to indices into it.
    QList<QString> names;
    QHash<QcBlockId, QcBlockId> localIds;
    QList<QcChunk> remapped;
    for (const std::shared_ptr<const QcChunk> &chunk : chunks) {
        QcChunk copy(chunk->pos());
        if (const QcStatus mapped = toLocalIds(*chunk, copy, localIds, names); !mapped) {
            return mapped;
        }
        remapped.append(copy);
    }

    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QcSaveFile::streamVersion);
    stream << quint32(names.size());
    for (const QString &name : std::as_const(names)) {
        stream << name;
    }
    stream << quint32(remapped.size());
    for (const QcChunk &chunk : std::as_const(remapped)) {
        QcProtocol::writeChunk(stream, chunk);
    }
    return QcSaveFile::write(regionPath(region), regionMagic, payload, regionVersion);
}

QcStatus QcRegionStorage::toLocalIds(const QcChunk &chunk, QcChunk &copy, QHash<QcBlockId, QcBlockId> &localIds,
                                     QList<QString> &names) const
{
    for (int s = 0; s < QcWorld::sectionCount; ++s) {
        const QcChunkSection &section = chunk.section(s);
        QList<QcBlockId> palette;
        for (const QcBlockId id : section.palette()) {
            if (!localIds.contains(id)) {
                localIds.insert(id, QcBlockId(names.size()));
                names.append(m_content->stateName(id));
            }
            palette.append(localIds.value(id));
        }
        if (!copy.section(s).assign(palette, section.indices())) {
            return qcError(u"Internal error while saving chunk"_s);
        }
    }
    return {};
}

QcResult<QcRegionStorage::Chunks> QcRegionStorage::read(QStringList &warnings) const
{
    Chunks chunks;
    const QDir regions(QDir(m_directory).filePath(u"regions"_s));
    const QStringList files = regions.entryList({u"r.*.qcr"_s}, QDir::Files, QDir::Name);
    for (const QString &fileName : files) {
        if (const QcStatus read = readRegion(regions.filePath(fileName), chunks, warnings); !read) {
            return qcError(read.error());
        }
    }
    return chunks;
}

QcStatus QcRegionStorage::readRegion(const QString &path, Chunks &chunks, QStringList &warnings) const
{
    quint16 version = 0;
    const QcResult<QByteArray> payload = QcSaveFile::read(path, regionMagic, regionVersion, &version);
    if (!payload) {
        return qcError(payload.error());
    }
    if (version < regionVersion) {
        return qcError(u"%1 is from an older development version"_s.arg(path));
    }

    QDataStream stream(*payload);
    stream.setVersion(QcSaveFile::streamVersion);
    const QcResult<QList<QcBlockId>> runtimeIds = readPalette(stream, path, warnings);
    if (!runtimeIds) {
        return qcError(runtimeIds.error());
    }
    quint32 chunkCount = 0;
    if (!QcStreamLimits::readCount(stream, quint32(regionSize * regionSize), chunkCount)) {
        return qcError(u"%1 is damaged (chunk count)"_s.arg(path));
    }

    for (quint32 i = 0; i < chunkCount; ++i) {
        if (const QcStatus read = readChunk(stream, path, *runtimeIds, chunks); !read) {
            return read;
        }
    }
    if (!stream.atEnd()) {
        return qcError(u"%1 is damaged (trailing data)"_s.arg(path));
    }
    return {};
}

QcResult<QList<QcBlockId>> QcRegionStorage::readPalette(QDataStream &stream, const QString &path, QStringList &warnings) const
{
    quint32 nameCount = 0;
    if (!QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions, nameCount)) {
        return qcError(u"%1 is damaged (block palette)"_s.arg(path));
    }

    const QString fileName = QFileInfo(path).fileName();
    QList<QcBlockId> runtimeIds;
    for (quint32 i = 0; i < nameCount; ++i) {
        QString name;
        stream >> name;
        QStringList problems;
        const std::optional<QcBlockId> id = m_content->stateId(name, &problems);
        if (!id) {
            warnings.append(u"%1: unknown block '%2' replaced by air"_s.arg(fileName, name));
        }
        for (const QString &problem : std::as_const(problems)) {
            warnings.append(u"%1: %2, default used"_s.arg(fileName, problem));
        }
        runtimeIds.append(id.value_or(qcAirBlockId));
    }
    return runtimeIds;
}

QcStatus QcRegionStorage::readChunk(QDataStream &stream, const QString &path, const QList<QcBlockId> &runtimeIds, Chunks &chunks) const
{
    QcResult<std::shared_ptr<QcChunk>> chunk = QcProtocol::readChunk(stream);
    if (!chunk) {
        return qcError(u"%1: %2"_s.arg(path, chunk.error()));
    }
    if (const QcStatus mapped = toRuntimeIds(**chunk, runtimeIds, path); !mapped) {
        return mapped;
    }

    const QcChunkPos pos = (*chunk)->pos();
    if (std::abs(qint64(pos.x)) > QcWorld::maxHorizontalChunk || std::abs(qint64(pos.z)) > QcWorld::maxHorizontalChunk
        || QFileInfo(regionPath(regionOf(pos))).fileName() != QFileInfo(path).fileName()) {
        return qcError(u"%1 contains a chunk of another region"_s.arg(path));
    }
    if (chunks.contains(pos)) {
        return qcError(u"%1 contains chunk %2, %3 twice"_s.arg(path).arg(pos.x).arg(pos.z));
    }
    chunks.insert((*chunk)->pos(), *chunk);
    return {};
}
