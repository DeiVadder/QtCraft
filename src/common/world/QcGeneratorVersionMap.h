// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcChunkPos.h"
#include "common/core/QcResult.h"
#include "common/world/QcChunkSet.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QList>
#include <QMap>

#include <optional>

/*! \brief Which chunks of a dimension are generated with an older generator version than the
    dimension's own (decision E-1010): a world switched to a newer version keeps the area it had
    explored at the older one, so every chunk generated before looks as it did.

    Per version a set of chunks (QcChunkSet); the sets never share a chunk, and a chunk in none of
    them is generated with the dimension's version (world.json). Its bytes() are the payload of the
    file QcVersionMapFile keeps it in:

      [quint32 version count] per version, ascending: [quint16 version]
      [quint32 byte count][QcChunkSet::bytes() of its chunks]

    A version without chunks is not kept. */
class QcGeneratorVersionMap
{
public:
    /*! Returns the version of \a chunk; std::nullopt when the map does not name it (the
        dimension's version). */
    [[nodiscard]] std::optional<int> versionAt(QcChunkPos chunk) const;
    /*! Returns the version of \a chunk, \a fallback when the map does not name it. */
    [[nodiscard]] int versionOr(QcChunkPos chunk, int fallback) const { return versionAt(chunk).value_or(fallback); }
    /*! Gives the chunks of \a chunks that the map does not name yet the version \a version (1 to
        QcGeneratorVersion::maxStored). Returns how many it added. */
    qsizetype mark(const QcChunkSet &chunks, int version);

    /*! Returns whether the map names no chunk. */
    [[nodiscard]] bool isEmpty() const { return m_versions.isEmpty(); }
    /*! Returns the versions with chunks, ascending. */
    [[nodiscard]] QList<int> versions() const { return m_versions.keys(); }
    /*! Returns the chunks of \a version (empty for a version without any). */
    [[nodiscard]] QcChunkSet chunksOf(int version) const { return m_versions.value(version); }
    /*! Returns every chunk the map names. */
    [[nodiscard]] QcChunkSet allChunks() const;

    /*! Returns the bytes of the map (see the class). */
    [[nodiscard]] QByteArray bytes() const;
    /*! Returns the map read from \a bytes, as bytes() writes it; an error saying what is wrong
        (cut short, a version out of range or out of order, a chunk named twice), without a file
        name. */
    [[nodiscard]] static QcResult<QcGeneratorVersionMap> parse(QByteArrayView bytes);

    bool operator==(const QcGeneratorVersionMap &) const = default;

private:
    // NOLINT(qc-member-access): the sets per version are the whole value; every method works on them.
    QMap<int, QcChunkSet> m_versions;
};
