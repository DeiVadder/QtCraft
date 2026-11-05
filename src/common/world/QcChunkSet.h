// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcChunkPos.h"
#include "common/core/QcResult.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QDataStream>
#include <QHash>
#include <QList>
#include <QSet>

/*! \brief A set of chunks kept as one bitmap per region of regionSize x regionSize chunks
    (decisions E-946, E-1010): the index of generated chunks, the explored area of a dimension and
    each version's part of a generator version map.

    It takes about 30 times less memory than a set of chunk positions, and its bytes() go into the
    files that keep it:

      [quint32 region count] per region, sorted by z then x: [qint32 x][qint32 z]
      [128 bytes, one bit per chunk slot (x + 32 * z within the region, lowest bit first)]

    Regions without chunks are not kept. */
class QcChunkSet
{
public:
    /*! The side of a region in chunks, as in the region files (QcRegionFile::regionSize). */
    static constexpr int regionSize = 32;
    /*! The bytes of the bitmap of one region. */
    static constexpr int bitmapBytes = regionSize * regionSize / 8;
    /*! The QDataStream version of bytes(), that of every save file (QcSaveFile::streamVersion,
        which asserts it is the same). */
    static constexpr QDataStream::Version streamVersion = QDataStream::Qt_6_8;

    /*! Returns the region that holds \a chunk. */
    [[nodiscard]] static QcChunkPos regionOf(QcChunkPos chunk);

    /*! Adds \a chunk, which must lie inside the world (QcChunkPos::isInsideWorld). Returns
        whether it was not in the set yet. */
    bool insert(QcChunkPos chunk);
    /*! Returns whether \a chunk is in the set. */
    [[nodiscard]] bool contains(QcChunkPos chunk) const;
    /*! Returns whether the set holds no chunk. */
    [[nodiscard]] bool isEmpty() const { return m_regions.isEmpty(); }
    /*! Returns the number of chunks in the set. */
    [[nodiscard]] qsizetype size() const;
    /*! Returns the number of regions with chunks in the set. */
    [[nodiscard]] qsizetype regionCount() const { return m_regions.size(); }
    /*! Returns the regions with chunks, sorted by z, then x. */
    [[nodiscard]] QList<QcChunkPos> regions() const;
    /*! Returns whether a region from \a lowest to \a highest (both inclusive, in regions) has
        chunks in the set. */
    [[nodiscard]] bool hasRegionWithin(QcChunkPos lowest, QcChunkPos highest) const;
    /*! Returns every chunk in the set (built from the bitmaps). */
    [[nodiscard]] QSet<QcChunkPos> chunks() const;

    /*! Adds every chunk of \a other. */
    void unite(const QcChunkSet &other);
    /*! Returns the chunks of this set that are not in \a other. */
    [[nodiscard]] QcChunkSet without(const QcChunkSet &other) const;
    /*! Returns whether a chunk is in both this set and \a other. */
    [[nodiscard]] bool intersects(const QcChunkSet &other) const;

    /*! Returns the bytes of the set (see the class). */
    [[nodiscard]] QByteArray bytes() const;
    /*! Returns the set read from \a bytes, as bytes() writes it; an error saying what is wrong
        (cut short, trailing bytes, a region beyond the world or twice), without a file name. */
    [[nodiscard]] static QcResult<QcChunkSet> parse(QByteArrayView bytes);

    bool operator==(const QcChunkSet &) const = default;

private:
    // NOLINT(qc-member-access): the bitmaps are the whole value; every method is a set operation on them.
    QHash<QcChunkPos, QByteArray> m_regions;
};
