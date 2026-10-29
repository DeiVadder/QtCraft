// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/auth/QcSecretFile.h"

#include "common/auth/QcOwnerOnlyAccess.h"
#include "common/core/QcLogging.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace QcSecretFile {

QcStatus write(const QString &path, const QByteArray &bytes)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return qcError(file.errorString());
    }
    // The temporary file gets the permissions before the secret goes in; the commit renames it
    // with them over the old file.
    if (!file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
        const QString reason = file.errorString();
        file.cancelWriting();
        return qcError(u"its permissions cannot be limited to the user (%1)"_s.arg(reason));
    }
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        return qcError(file.errorString());
    }
    // Windows: the permission bits above only clear the read-only attribute; the file gets its own
    // access list after the commit (QSaveFile names its temporary file itself). A file system
    // without access lists (FAT) keeps the file usable, with a warning.
    if (!QcOwnerOnlyAccess::restrictToOwner(path)) {
        qCWarning(lcConnection).noquote() << u"Cannot limit the access to %1 to this user; keep the data folder in your user profile"_s.arg(
            QDir::toNativeSeparators(path));
    }
    return {};
}

bool othersMayRead(const QString &path)
{
#ifdef Q_OS_WIN
    // Qt derives all read bits from the read-only attribute there; who may read is the file's
    // access list.
    return QcOwnerOnlyAccess::othersMayRead(path);
#else
    constexpr QFileDevice::Permissions others = QFileDevice::ReadGroup | QFileDevice::ReadOther;
    return (QFile::permissions(path) & others) != QFileDevice::Permissions{};
#endif
}

} // namespace QcSecretFile
