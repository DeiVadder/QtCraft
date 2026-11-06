// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QMap>

namespace QcTestUtil {

// Every file below `directory` with a hash of its bytes, by path.
inline QMap<QString, QByteArray> filesOf(const QString &directory)
{
    QMap<QString, QByteArray> files;
    QDirIterator it(directory, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            files.insert(QDir(directory).relativeFilePath(path), QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha1));
        }
    }
    return files;
}

} // namespace QcTestUtil
