#include "server/commands/QcWorldBackup.h"

#include "server/storage/QcServerPersistence.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>

#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace Qt::StringLiterals;

namespace QcWorldBackup {

namespace {

struct Listing
{
    QStringList files;
    qint64 bytes = 0;
};

// Every regular file below the world directory (relative paths) without the session lock.
[[nodiscard]] QcResult<Listing> list(const QString &worldDirectory)
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

// The staging folder and the incomplete copy of a target, both hidden in the backup root.
QString stagingOf(const QString &target)
{
    const QFileInfo info(target);
    return QDir(info.absolutePath()).filePath(u".%1.staging"_s.arg(info.fileName()));
}

QString incompleteOf(const QString &target)
{
    const QFileInfo info(target);
    return QDir(info.absolutePath()).filePath(u".%1.incomplete"_s.arg(info.fileName()));
}

QString freeTarget(const QDir &root, const QString &base)
{
    QString name = base;
    for (int attempt = 2; root.exists(name) || QFile::exists(stagingOf(root.filePath(name))); ++attempt) {
        name = u"%1-%2"_s.arg(base).arg(attempt);
    }
    return root.filePath(name);
}

// A second name for the same file contents; false where the file system cannot do it.
[[nodiscard]] bool hardLink(const QString &from, const QString &to)
{
#ifdef Q_OS_WIN
    return CreateHardLinkW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(to).utf16()),
                           reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(from).utf16()), nullptr) != 0;
#else
    return ::link(QFile::encodeName(from).constData(), QFile::encodeName(to).constData()) == 0;
#endif
}

// Copies every listed file below `from` into `to` (made first), with hard links where `link`; the
// file that could not be copied, std::nullopt when all were.
[[nodiscard]] std::optional<QString> copyFiles(const QString &from, const QString &to, const QStringList &files, bool link)
{
    const QDir source(from);
    for (const QString &file : files) {
        const QString destination = QDir(to).filePath(file);
        const bool made = QDir().mkpath(QFileInfo(destination).absolutePath())
                          && ((link && hardLink(source.filePath(file), destination)) || QFile::copy(source.filePath(file), destination));
        if (!made) {
            return file;
        }
    }
    return std::nullopt;
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

[[nodiscard]] QcResult<Snapshot> snapshot(const QString &worldDirectory, const QString &root, const QString &label, const QDateTime &now)
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
    Snapshot snapshot{{}, freeTarget(QDir(root), base), listing->files, listing->bytes, true};
    snapshot.staging = stagingOf(snapshot.target);

    // Hard links where the file system has them (the first file tells), copies otherwise.
    snapshot.linked = !listing->files.isEmpty() && QDir().mkpath(QFileInfo(QDir(snapshot.staging).filePath(listing->files.first())).absolutePath())
                      && hardLink(QDir(worldDirectory).filePath(listing->files.first()), QDir(snapshot.staging).filePath(listing->files.first()));
    QStringList rest = listing->files;
    if (snapshot.linked) {
        rest.removeFirst();
    }
    if (const std::optional<QString> failed = copyFiles(worldDirectory, snapshot.staging, rest, snapshot.linked)) {
        QDir(snapshot.staging).removeRecursively();
        return qcError(u"Cannot copy %1, the incomplete backup was removed"_s.arg(*failed));
    }
    return snapshot;
}

[[nodiscard]] QcResult<Summary> complete(const Snapshot &snapshot)
{
    // Written under a hidden name: an interrupted copy never looks like a backup. A snapshot of
    // copies already is the backup.
    const QString incomplete = incompleteOf(snapshot.target);
    QString problem;
    if (snapshot.linked) {
        if (const std::optional<QString> failed = copyFiles(snapshot.staging, incomplete, snapshot.files, false)) {
            problem = u"Cannot copy %1, the incomplete backup was removed"_s.arg(*failed);
        }
    } else if (!QDir().rename(snapshot.staging, incomplete)) {
        problem = u"Cannot move the copied files from %1 to %2, the incomplete backup was removed"_s.arg(snapshot.staging, incomplete);
    }
    QDir(snapshot.staging).removeRecursively();
    if (problem.isEmpty() && !QDir().rename(incomplete, snapshot.target)) {
        problem = u"Cannot rename the backup to %1"_s.arg(snapshot.target);
    }
    if (!problem.isEmpty()) {
        QDir(incomplete).removeRecursively();
        return qcError(problem);
    }
    return Summary{snapshot.target, int(snapshot.files.size()), snapshot.bytes};
}

[[nodiscard]] QcResult<Summary> copy(const QString &worldDirectory, const QString &root, const QString &label, const QDateTime &now)
{
    const QcResult<Snapshot> frozen = snapshot(worldDirectory, root, label, now);
    if (!frozen) {
        return qcError(frozen.error());
    }
    return complete(*frozen);
}

} // namespace QcWorldBackup
