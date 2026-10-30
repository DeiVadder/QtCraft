// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcChunkPos.h"
#include "common/core/QcResult.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QHash>
#include <QList>
#include <QString>

#include <optional>

/*! \brief One region file of a world (decision E-373).

    It holds the data of up to regionSize x regionSize chunks of one kind (blocks, block entities or
    entities), each chunk a separate blob found through an index at the start, so one chunk is read
    without reading or decoding the others:

      [magic 4][quint16 version][quint32 entry count][quint32 crc of the entries]
      entries: [quint16 slot (x + regionSize * z within the region)][quint16 marks]
               [quint32 offset][quint32 size][quint32 crc of the blob]
      blobs
    \c {marks} is a small value its owner keeps beside a chunk without reading the blob (the
    circuit marks of QcChunkLinks). A damaged index makes the file unreadable; a damaged blob
    only its chunk. */
namespace QcRegionFile {

/*! The side of a region in chunks. */
inline constexpr int regionSize = 32;
/*! The chunks of a region. */
inline constexpr int chunksPerRegion = regionSize * regionSize;
/*! Bigger blobs are damaged data (a chunk of blocks is far smaller). */
inline constexpr quint32 maxBlobBytes = 64u * 1024 * 1024;

/*! \brief The index entry of a chunk: its marks, and offset, size and checksum of its blob.
    \qcfields */
struct Entry
{
    quint16 marks = 0;
    quint32 offset = 0;
    quint32 size = 0;
    quint32 crc = 0;
};

/*! The index of a file by chunk. */
using Index = QHash<QcChunkPos, Entry>;

/*! \brief The blob of a chunk to write or as read. */
struct Blob
{
    /*! The chunk. */
    QcChunkPos chunk;
    /*! Its marks. */
    quint16 marks = 0;
    /*! The blob's bytes. */
    QByteArray data;
    /*! The checksum a blob kept from an older file had there: copied as it was, so a damaged
        blob stays recognisably damaged instead of getting a fresh checksum. None: computed from
        \c {data}. */
    std::optional<quint32> crc;
};

/*! Returns the region that holds \a chunk. */
QcChunkPos regionOf(QcChunkPos chunk);
/*! Returns the file of \a region in \a folder with \a extension:
    <folder>/r.<x>.<z>.<extension>. */
QString path(const QString &folder, QcChunkPos region, const QString &extension);
/*! Returns the region \a fileName names, if it is one (r.<x>.<z>.<extension> with
    \a extension). */
[[nodiscard]] std::optional<QcChunkPos> regionOfFile(const QString &fileName, const QString &extension);

/*! Returns the index read from the file \a path of \a region, which starts with \a magic; a
    chunk of another region, a slot twice or an entry beyond the end of the file is damage.
    \a version must match: older development versions are refused (decision E-356). */
[[nodiscard]] QcResult<Index> readIndex(const QString &path, QcChunkPos region, QByteArrayView magic, quint16 version);
/*! Returns the blob of \a entry read from the file \a path, checked against its crc. */
[[nodiscard]] QcResult<QByteArray> readBlob(const QString &path, Entry entry);
/*! \brief Every blob of a file, and the chunks whose bytes do not match their checksum.
    \qcfields */
struct AllBlobs
{
    QList<Blob> blobs;
    QList<QcChunkPos> damaged;
};
/*! Returns every blob of the file \a path (as \a index lists them) in one read with the marks
    and checksum of its entry (rewriting a region keeps the blobs that did not change, damaged
    ones included). */
[[nodiscard]] QcResult<AllBlobs> readAll(const QString &path, const Index &index);
/*! Writes \a blobs as a whole file at \a path with \a magic and \a version (no staging;
    QcChunkStore stages and renames). Returns the index of what was written. */
[[nodiscard]] QcResult<Index> write(const QString &path, QByteArrayView magic, quint16 version, const QList<Blob> &blobs);

} // namespace QcRegionFile
