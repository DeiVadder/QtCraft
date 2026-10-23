#pragma once

#include "common/core/QcResult.h"
#include "common/maps/QcMapData.h"

#include <QByteArray>
#include <QString>

class QcContentRegistry;

// One map in the world folder (decision E-799), maps/<id>.qcmap in the save file container
// (QcSaveFile): its scale, centre and world ("overworld", the only one so far), the names of the
// blocks it shows and its pixels as indices into that list with their shades, compressed. Blocks
// a later content no longer knows read as blank pixels. Every value is checked when read.
namespace QcMapSaveCodec {

inline constexpr QByteArrayView magic = "QCMP";
inline constexpr QStringView overworld = u"overworld";
// The longest file a map may have (a map of noise compresses to about 40 KiB).
inline constexpr qsizetype maxBytes = qsizetype{256} * 1024;

QString fileName(quint32 id);
// The id of a map file name ("maps/12.qcmap" or "12.qcmap"), std::nullopt for any other name and
// for ids beyond QcMapStore::maxId.
[[nodiscard]] std::optional<quint32> idOf(const QString &fileName);

[[nodiscard]] QByteArray write(const QcMapData &map, const QcContentRegistry &content);
[[nodiscard]] QcResult<QcMapData> read(const QByteArray &file, const QString &path, const QcContentRegistry &content);

} // namespace QcMapSaveCodec
