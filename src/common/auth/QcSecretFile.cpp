#include "common/auth/QcSecretFile.h"

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
    return {};
}

bool othersMayRead(const QString &path)
{
    constexpr QFileDevice::Permissions others = QFileDevice::ReadGroup | QFileDevice::ReadOther;
    return (QFile::permissions(path) & others) != QFileDevice::Permissions{};
}

} // namespace QcSecretFile
