// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"
#include "common/maps/QcMapData.h"

#include <QByteArray>
#include <QString>

class QcContentRegistry;

/*! \brief One map in the world folder (decision E-799), maps/<id>.qcmap in the save file container
    (QcSaveFile): its scale, centre and dimension (its qualified name, decision E-1005; files from
    before dimensions say "overworld"), the names of the
    blocks it shows and its pixels as indices into that list with their shades, compressed.

    Blocks a later content no longer knows read as blank pixels. Every value is checked when
    read. */
namespace QcMapSaveCodec {

/*! The container's magic of a map file. */
inline constexpr QByteArrayView magic = "QCMP";
/*! The name files from before dimensions give the world a map shows: the overworld. */
inline constexpr QStringView overworld = u"overworld";
/*! The longest file a map may have (a map of noise compresses to about 40 KiB). */
inline constexpr qsizetype maxBytes = qsizetype{256} * 1024;

/*! Returns the file name of map \a id below the world folder ("maps/<id>.qcmap"). */
QString fileName(quint32 id);
/*! Returns the id of map file name \a fileName ("maps/12.qcmap" or "12.qcmap"), std::nullopt for
    any other name and for ids beyond QcMapStore::maxId. */
[[nodiscard]] std::optional<quint32> idOf(const QString &fileName);

/*! Returns the file of \a map with the block names of \a content. */
[[nodiscard]] QByteArray write(const QcMapData &map, const QcContentRegistry &content);
/*! Returns the map read from \a file, its blocks looked up in \a content (unknown ones read as
    blank pixels), or an error naming \a path when the file is too large, damaged or not a map. */
[[nodiscard]] QcResult<QcMapData> read(const QByteArray &file, const QString &path, const QcContentRegistry &content);

} // namespace QcMapSaveCodec
