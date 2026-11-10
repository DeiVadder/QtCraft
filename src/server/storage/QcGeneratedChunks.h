// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcChunkPos.h"
#include "common/core/QcResult.h"
#include "common/world/QcChunkSet.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QStringView>

/*! \brief The index of the generated chunks of a world (decision E-946): every chunk that was ever
    in the world, whether its blocks changed or not, so a later generator change knows the explored
    area.

    It lives in the file generated.qcg of the world directory, written whole with a commit of
    QcChunkStore (and so through its journal):

      QcSaveFile container (magic, formatVersion), payload: [quint32 region count]
      per region, sorted by z then x: [qint32 x][qint32 z][128 bytes, one bit per chunk slot
      (x + 32 * z within the region, lowest bit first)]

    In memory it keeps the same bitmap per region (QcChunkSet, whose bytes() are the payload). A
    world without the file starts with an empty index. Entries are only ever added. */
class QcGeneratedChunks
{
public:
    /*! Name of the file in the world directory. */
    static constexpr QStringView fileName = u"generated.qcg";
    /*! Name of the file this build writes instead while generated.qcg has a newer format version,
        which it leaves alone (decision E-946). */
    static constexpr QStringView olderFileName = u"generated.v1.qcg";
    /*! The magic of the file. */
    static constexpr QByteArrayView magic = "QCGC";
    /*! The format version this build reads and writes. */
    static constexpr quint16 formatVersion = 1;

    struct Opened;
    /*! Returns the index of the world directory \a directory (decision E-946): none without the
        file; a damaged file is kept aside (and removed once the copy exists) and the index starts
        empty; while generated.qcg has a newer format version it stays untouched, and the index is
        the one this build keeps in olderFileName. */
    [[nodiscard]] static Opened open(const QString &directory);

    /*! Returns the index read from \a payload, the payload of the file \a path (named in
        messages); an error when it is damaged or names a region beyond the world. */
    [[nodiscard]] static QcResult<QcGeneratedChunks> parse(const QByteArray &payload, const QString &path);

    /*! Adds \a chunk; a chunk not in the index yet makes it unsaved. */
    void note(QcChunkPos chunk);
    /*! Returns whether \a chunk is in the index. */
    bool contains(QcChunkPos chunk) const { return m_chunks.contains(chunk); }
    /*! Returns every chunk in the index (built from the bitmaps). */
    QSet<QcChunkPos> chunks() const { return m_chunks.chunks(); }
    /*! Returns the chunks of the index as a set of region bitmaps. */
    const QcChunkSet &chunkSet() const { return m_chunks; }
    /*! Returns the number of regions with chunks in the index. */
    qsizetype regionCount() const { return m_chunks.regionCount(); }

    /*! Returns whether chunks were added since the index was last written or read. */
    bool isUnsaved() const { return m_unsaved; }
    /*! Sets whether chunks were added since the index was last written (\a unsaved). */
    void setUnsaved(bool unsaved) { m_unsaved = unsaved; }

    /*! Returns the bytes of the file. */
    QByteArray bytes() const;

private:
    // The index of the file `name` in `directory`, read as open() describes (`newer`: the file has a
    // newer format version and is left alone, the index is empty).
    [[nodiscard]] static Opened load(const QString &directory, const QString &name);

    // One bit per chunk slot of each region with chunks.
    // NOLINT(qc-member-access): the index is this set; note() adds to it, parse() reads it in.
    QcChunkSet m_chunks;
    bool m_unsaved = false;
};

/*! \brief The index as found in a world directory: the index, the file (relative to the
    directory) the next save writes it to, whether generated.qcg has a newer format version, and
    warnings for the player. \qcfields */
struct QcGeneratedChunks::Opened
{
    QcGeneratedChunks index;
    QString file;
    bool newer = false;
    QStringList warnings;
};
