#include "QcWorldBackup.h"

#include "QcServerPersistence.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace QcWorldBackup {

namespace {

struct Listing
{
    QStringList files;
    qint64 bytes = 0;
};

// Every regular file below the world directory (relative paths) without the session lock.
QcResult<Listing> list(const QString &worldDirectory)
{
    Listing listing;
    const QDir root(worldDirectory);
    QDirIterator it(worldDirectory, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        if (info.isSymLink()) {
            return qcError(u"The world folder contains a symbolic link (%1); it is not copied"_s.arg(info.filePath()));
        }
        if (info.isDir() || info.fileName() == QcServerPersistence::lockFileName) {
            continue;
        }

        listing.files.append(root.relativeFilePath(info.filePath()));
        listing.bytes += info.size();
        if (listing.bytes > maxWorldBytes) {
            return qcError(u"The world is larger than %1 MB"_s.arg(maxWorldBytes / (qint64(1024) * 1024)));
        }
    }
    return listing;
}

// The absolute path with symbolic links resolved as far as it exists (the rest is appended).
QString resolved(const QString &path)
{
    QFileInfo info(QDir::cleanPath(QDir(path).absolutePath()));
    QStringList missing;
    while (!info.exists() && !info.isRoot() && info.absolutePath() != info.absoluteFilePath()) {
        missing.prepend(info.fileName());
        info = QFileInfo(info.absolutePath());
    }
    const QString existing = info.exists() ? info.canonicalFilePath() : info.absoluteFilePath();
    return missing.isEmpty() ? existing : QDir::cleanPath(existing + u'/' + missing.join(u'/'));
}

QString freeTarget(const QDir &root, const QString &base)
{
    QString name = base;
    for (int attempt = 2; root.exists(name); ++attempt) {
        name = u"%1-%2"_s.arg(base).arg(attempt);
    }
    return root.filePath(name);
}

} // namespace

QString defaultRoot(const QString &worldDirectory)
{
    return QDir::cleanPath(QDir(worldDirectory).absoluteFilePath(u"../../backups"_s));
}

bool isValidLabel(const QString &label)
{
    return label.size() <= maxLabelLength && std::ranges::all_of(label, [](QChar c) {
        return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9') || c == u'_' || c == u'-';
    });
}

QcResult<Summary> copy(const QString &worldDirectory, const QString &root, const QString &label, const QDateTime &now)
{
    if (!isValidLabel(label)) {
        return qcError(u"A label may only have up to %1 letters, digits, '_' and '-'"_s.arg(maxLabelLength));
    }

    // A backup inside the world would be copied into the next one, and so on.
    const QString world = resolved(worldDirectory);
    const QString target = resolved(root);
    if (target == world || target.startsWith(world + u'/')) {
        return qcError(u"The backup folder %1 lies inside the world folder"_s.arg(root));
    }

    const QcResult<Listing> listing = list(worldDirectory);
    if (!listing) {
        return qcError(listing.error());
    }
    if (!QDir().mkpath(root)) {
        return qcError(u"Cannot create the backup folder %1"_s.arg(root));
    }

    const QStorageInfo storage(root);
    if (storage.isValid() && storage.bytesAvailable() < listing->bytes + reserveBytes) {
        return qcError(u"Not enough free disk space for the backup (%1 MB needed)"_s.arg((listing->bytes + reserveBytes) / (qint64(1024) * 1024)));
    }

    QString base = u"%1-%2"_s.arg(QFileInfo(worldDirectory).fileName(), now.toString(u"yyyyMMdd-HHmmss"_s));
    if (!label.isEmpty()) {
        base += u'-' + label;
    }
    const QString folder = freeTarget(QDir(root), base);
    const QDir source(worldDirectory);
    for (const QString &file : listing->files) {
        const QString destination = QDir(folder).filePath(file);
        if (!QDir().mkpath(QFileInfo(destination).absolutePath()) || !QFile::copy(source.filePath(file), destination)) {
            // The folder is new (freeTarget): an incomplete copy must not pass for a backup.
            QDir(folder).removeRecursively();
            return qcError(u"Cannot copy %1, the incomplete backup was removed"_s.arg(file));
        }
    }
    return Summary{folder, int(listing->files.size()), listing->bytes};
}

} // namespace QcWorldBackup
