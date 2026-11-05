// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/versions/QcVersionMapFile.h"

#include "server/storage/QcSaveFile.h"

#include <QDir>
#include <QFile>

#include <limits>

using namespace Qt::StringLiterals;

namespace QcVersionMapFile {

QcResult<QcGeneratorVersionMap> read(const QString &directory)
{
    const QString path = QDir(directory).filePath(fileName.toString());
    if (!QFile::exists(path)) {
        return QcGeneratorVersionMap{};
    }
    // Any version passes the container check here, so a newer one is told apart from damage.
    const QcResult<QcSaveFile::Payload> payload = QcSaveFile::read(path, magic, std::numeric_limits<quint16>::max());
    if (!payload) {
        return qcError(payload.error());
    }
    if (payload->version > formatVersion) {
        return qcError(u"%1 has format version %2, newer than this version of QtCraft reads (%3)"_s.arg(path).arg(payload->version).arg(formatVersion));
    }
    QcResult<QcGeneratorVersionMap> map = QcGeneratorVersionMap::parse(payload->bytes);
    if (!map) {
        return qcError(u"%1 is damaged (%2)"_s.arg(path, map.error()));
    }
    return map;
}

QcStatus write(const QString &directory, const QcGeneratorVersionMap &map)
{
    const QString path = QDir(directory).filePath(fileName.toString());
    if (map.isEmpty()) {
        if (QFile::exists(path) && !QFile::remove(path)) {
            return qcError(u"Cannot remove %1"_s.arg(path));
        }
        return {};
    }
    if (!QDir().mkpath(directory)) {
        return qcError(u"Cannot create %1"_s.arg(directory));
    }
    return QcSaveFile::write(path, magic, map.bytes(), formatVersion);
}

} // namespace QcVersionMapFile
