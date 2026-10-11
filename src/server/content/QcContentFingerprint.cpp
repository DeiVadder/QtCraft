#include "QcContentFingerprint.h"

#include "QcBaseContent.h"
#include "QcContentPackLoader.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcFingerprint, "qtcraft.content")

constexpr qsizetype hashBytes = 16;

// The files the content loader reads, relative to the pack's directory.
QcResult<QStringList> filesBelow(const QString &directory)
{
    const QDir root(directory);
    QStringList files{u"pack.json"_s};
    const QStringList contentFiles = QcContentPackLoader::contentFiles(directory);
    for (const QString &path : contentFiles) {
        files.append(root.relativeFilePath(path));
    }
    if (files.size() > QcContentFingerprint::maxPackFiles) {
        return qcError(u"%1 has more than %2 files"_s.arg(directory).arg(QcContentFingerprint::maxPackFiles));
    }
    files.sort();
    return files;
}

} // namespace

namespace QcContentFingerprint {

QcResult<QString> hashOf(const QString &directory)
{
    const QcResult<QStringList> files = filesBelow(directory);
    if (!files) {
        return qcError(files.error());
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 total = 0;
    const QDir root(directory);
    for (const QString &name : *files) {
        QFile file(root.filePath(name));
        total += file.size();
        if (total > maxPackBytes || !file.open(QIODevice::ReadOnly)) {
            return qcError(total > maxPackBytes ? u"%1 is larger than %2 MB"_s.arg(directory).arg(maxPackBytes >> 20)
                                                : u"Cannot read %1"_s.arg(file.fileName()));
        }

        // Path and size first, so that moving bytes from one file to the next changes the hash.
        hash.addData(name.toUtf8());
        hash.addData(QByteArrayView("\0", 1));
        hash.addData(QByteArray::number(file.size()));
        hash.addData(QByteArrayView("\0", 1));
        if (!hash.addData(&file)) {
            return qcError(u"Cannot read %1"_s.arg(file.fileName()));
        }
    }
    return QString::fromLatin1(hash.result().first(hashBytes).toHex());
}

QList<QcPackFingerprint> of(const QcContentRegistry &content, const QList<QcModStatus> &mods)
{
    QHash<QString, QString> directories{{content.packs().isEmpty() ? QString() : content.packs().first().id, QString::fromLatin1(QcBaseContent::resourceDirectory)}};
    for (const QcModStatus &mod : mods) {
        if (mod.state == QcModStatus::State::Loaded) {
            directories.insert(mod.info.id, mod.info.directory);
        }
    }

    QList<QcPackFingerprint> packs;
    for (const QcPackReference &pack : content.packs()) {
        const QString directory = directories.value(pack.id);
        const QcResult<QString> hash = directory.isEmpty() ? QcResult<QString>(qcError(u"no directory"_s)) : hashOf(directory);
        if (!hash) {
            qCWarning(lcFingerprint).noquote() << u"Content pack %1 is compared without a hash: %2"_s.arg(pack.id, hash.error());
        }
        packs.append({pack.id, pack.version, hash.value_or(QString())});
    }
    return packs;
}

} // namespace QcContentFingerprint
