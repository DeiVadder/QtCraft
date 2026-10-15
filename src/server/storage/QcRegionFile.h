#pragma once

#include "QcChunkPos.h"
#include "QcResult.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QHash>
#include <QList>
#include <QString>

#include <optional>

// One region file of a world (decision E-373): the data of up to regionSize x regionSize chunks of
// one kind (blocks, block entities or entities), each chunk a separate blob found through an index
// at the start, so one chunk is read without reading or decoding the others:
//   [magic 4][quint16 version][quint32 entry count][quint32 crc of the entries]
//   entries: [quint16 slot (x + regionSize * z within the region)][quint16 marks]
//            [quint32 offset][quint32 size][quint32 crc of the blob]
//   blobs
// `marks` is a small value its owner keeps beside a chunk without reading the blob (the circuit
// marks of QcChunkLinks). A damaged index makes the file unreadable; a damaged blob only its chunk.
namespace QcRegionFile {

inline constexpr int regionSize = 32;
inline constexpr int chunksPerRegion = regionSize * regionSize;
// Bigger blobs are damaged data (a chunk of blocks is far smaller).
inline constexpr quint32 maxBlobBytes = 64u * 1024 * 1024;

struct Entry
{
    quint16 marks = 0;
    quint32 offset = 0;
    quint32 size = 0;
    quint32 crc = 0;
};

using Index = QHash<QcChunkPos, Entry>;

struct Blob
{
    QcChunkPos chunk;
    quint16 marks = 0;
    QByteArray data;
    // The checksum a blob kept from an older file had there: copied as it was, so a damaged blob
    // stays recognisably damaged instead of getting a fresh checksum. None: computed from `data`.
    std::optional<quint32> crc;
};

QcChunkPos regionOf(QcChunkPos chunk);
// The file of `region` in `folder` with this extension: <folder>/r.<x>.<z>.<extension>.
QString path(const QString &folder, QcChunkPos region, const QString &extension);
// The region a file name names, if it is one (r.<x>.<z>.<extension>).
std::optional<QcChunkPos> regionOfFile(const QString &fileName, const QString &extension);

// Reads the index of the file of `region`; a chunk of another region, a slot twice or an entry
// beyond the end of the file is damage. `version` must match: older development versions are
// refused (decision E-356).
QcResult<Index> readIndex(const QString &path, QcChunkPos region, QByteArrayView magic, quint16 version);
// One blob, checked against its crc.
QcResult<QByteArray> readBlob(const QString &path, Entry entry);
// Every blob of the file in one read with the marks and checksum of its entry (rewriting a region
// keeps the blobs that did not change, damaged ones included); `damaged` gets the chunks whose
// bytes do not match their checksum.
QcResult<QList<Blob>> readAll(const QString &path, const Index &index, QList<QcChunkPos> *damaged = nullptr);
// Writes a whole file at `path` (no staging; QcChunkStore stages and renames). Returns the index
// of what was written.
QcResult<Index> write(const QString &path, QByteArrayView magic, quint16 version, const QList<Blob> &blobs);

} // namespace QcRegionFile
