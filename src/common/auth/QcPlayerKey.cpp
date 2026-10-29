// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/auth/QcPlayerKey.h"

#include "common/auth/QcLoginCrypto.h"
#include "common/auth/QcSecretFile.h"
#include "common/core/QcDataRoot.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

using namespace Qt::StringLiterals;

namespace QcPlayerKey {

QString defaultPath()
{
    return QDir(QcDataRoot::path()).filePath(QString::fromLatin1(fileName));
}

[[nodiscard]] QcResult<QByteArray> loadOrCreate(const QString &path)
{
    QFile existing(path);
    if (existing.exists()) {
        if (!existing.open(QIODevice::ReadOnly)) {
            return qcError(u"Cannot read the player key %1: %2"_s.arg(path, existing.errorString()));
        }
        const QByteArray hex = existing.read(QcLoginCrypto::keyBytes * 2 + 2).trimmed();
        QByteArray key = QByteArray::fromHex(hex);
        if (hex.size() != QcLoginCrypto::keyBytes * 2 || key.size() != QcLoginCrypto::keyBytes || key.toHex() != hex.toLower()) {
            return qcError(u"The player key %1 is damaged; move it away to start with a new identity"_s.arg(path));
        }
        return key;
    }

    QByteArray key = QcLoginCrypto::randomBytes(QcLoginCrypto::keyBytes);
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (const QcStatus written = QcSecretFile::write(path, key.toHex() + '\n'); !written) {
        return qcError(u"Cannot write the player key %1: %2"_s.arg(path, written.error()));
    }
    return key;
}

} // namespace QcPlayerKey
