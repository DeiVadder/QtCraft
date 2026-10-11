#include "QcPlayerKey.h"

#include "QcDataRoot.h"
#include "QcLoginCrypto.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace QcPlayerKey {

QString defaultPath()
{
    return QDir(QcDataRoot::path()).filePath(QString::fromLatin1(fileName));
}

QcResult<QByteArray> loadOrCreate(const QString &path)
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
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(key.toHex() + '\n') < 0) {
        return qcError(u"Cannot write the player key %1: %2"_s.arg(path, file.errorString()));
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (!file.commit()) {
        return qcError(u"Cannot write the player key %1: %2"_s.arg(path, file.errorString()));
    }
    return key;
}

} // namespace QcPlayerKey
