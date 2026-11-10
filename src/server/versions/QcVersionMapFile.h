// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"
#include "common/world/QcGeneratorVersionMap.h"

#include <QByteArrayView>
#include <QString>
#include <QStringView>

/*! \brief The generator version map of a dimension on disk (decision E-1010): the file
    generatorversions.qcv in the directory of the dimension's chunks
    (QcWorldStorage::dimensionDirectory), the payload QcGeneratorVersionMap::bytes() in the
    QcSaveFile container.

    Only switching the generator version of a world writes it, while the world is closed; the
    server only reads it. Unlike the index of generated chunks it decides how chunks look, so a
    damaged file or one of a newer format keeps the world from opening. */
namespace QcVersionMapFile {

/*! Name of the file in the directory of a dimension. */
inline constexpr QStringView fileName = u"generatorversions.qcv";
/*! The magic of the file. */
inline constexpr QByteArrayView magic = "QCVM";
/*! The format version this build reads and writes. */
inline constexpr quint16 formatVersion = 1;

/*! Returns the map kept in \a directory, the directory of a dimension; an empty map without the
    file, an error naming the file when it is damaged or has a newer format. */
[[nodiscard]] QcResult<QcGeneratorVersionMap> read(const QString &directory);
/*! Writes \a map to \a directory (replacing the file as a whole; an empty map removes it).
    Returns an error when it cannot be written. */
[[nodiscard]] QcStatus write(const QString &directory, const QcGeneratorVersionMap &map);

} // namespace QcVersionMapFile
