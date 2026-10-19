#include "worlds/QcBundledMods.h"

#include "worlds/QcModLoader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;

namespace {

// Copies the regular files and real directories below `from` into `to`. The files copied before
// (`copied`) and here count against QcBundledMods::maxFiles; returns that count.
[[nodiscard]] QcResult<int> copyTree(const QString &from, const QString &to, int depth, int copied)
{
    int files = copied;
    if (depth > QcBundledMods::maxDepth) {
        return qcError(u"%1 is nested too deeply"_s.arg(from));
    }
    if (!QDir().mkpath(to)) {
        return qcError(u"Cannot create %1"_s.arg(to));
    }

    const QFileInfoList entries = QDir(from).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                                                           QDir::Name);
    for (const QFileInfo &entry : entries) {
        if (entry.isSymLink() || entry.fileName().startsWith(u'.')) {
            continue;
        }

        const QString target = QDir(to).filePath(entry.fileName());
        if (entry.isDir()) {
            const QcResult<int> below = copyTree(entry.filePath(), target, depth + 1, files);
            if (!below) {
                return below;
            }
            files = *below;
        } else if (entry.isFile()) {
            if (++files > QcBundledMods::maxFiles) {
                return qcError(u"More than %1 files in the bundled mods"_s.arg(QcBundledMods::maxFiles));
            }
            if (!QFile::copy(entry.filePath(), target)) {
                return qcError(u"Cannot copy %1"_s.arg(entry.filePath()));
            }
        }
    }
    return files;
}

} // namespace

namespace QcBundledMods {

QString directory(const QString &applicationDirectory)
{
    const QDir application(applicationDirectory);
    for (const QString &candidate : {u"../Resources/mods"_s, u"mods"_s}) {
        const QFileInfo info(application.filePath(candidate));
        if (info.isDir() && !info.isSymLink()) {
            return info.canonicalFilePath();
        }
    }
    return {};
}

[[nodiscard]] QcResult<QStringList> installDisabled(const QString &source, const QString &modsDirectory)
{
    if (source.isEmpty() || modsDirectory.isEmpty() || QFileInfo::exists(modsDirectory)) {
        return QStringList();
    }

    QFileInfoList mods;
    const QFileInfoList candidates = QDir(source).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &candidate : candidates) {
        const QFileInfo manifest(QDir(candidate.filePath()).filePath(u"pack.json"_s));
        if (!candidate.isSymLink() && manifest.isFile() && !manifest.isSymLink()) {
            mods.append(candidate);
        }
    }

    if (mods.isEmpty()) {
        return QStringList();
    }

    const QFileInfo target(QDir::cleanPath(modsDirectory));
    if (!QDir().mkpath(target.path())) {
        return qcError(u"Cannot create %1"_s.arg(target.path()));
    }

    // A fresh folder beside the target; it removes itself unless it was renamed into place.
    const QTemporaryDir staging(QDir(target.path()).filePath(target.fileName() + u".installing-XXXXXX"_s));
    if (!staging.isValid()) {
        return qcError(u"Cannot create a folder beside %1"_s.arg(modsDirectory));
    }

    int files = 0;
    for (const QFileInfo &mod : std::as_const(mods)) {
        const QcResult<int> copied = copyTree(mod.filePath(), staging.filePath(mod.fileName()), 0, files);
        if (!copied) {
            return std::unexpected(copied.error());
        }
        files = *copied;
    }

    QStringList ids;
    const QList<QcModStatus> found = QcModLoader::discover(staging.path());
    for (const QcModStatus &mod : found) {
        if (mod.info.id.isEmpty()) {
            continue;
        }
        if (const QcStatus disabled = QcModLoader::setModEnabled(staging.path(), mod.info.id, false); !disabled) {
            return std::unexpected(disabled.error());
        }
        ids.append(mod.info.id);
    }

    // QTemporaryDir is owner-only; the mods folder gets the same rights as one the player creates.
    const QFileDevice::Permissions folder = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                                            | QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther
                                            | QFileDevice::ExeOther;
    if (!QFile::setPermissions(staging.path(), folder)) {
        return qcError(u"Cannot set the rights of %1"_s.arg(staging.path()));
    }
    if (!QDir().rename(staging.path(), modsDirectory)) {
        return qcError(u"Cannot move the bundled mods to %1"_s.arg(modsDirectory));
    }
    return ids;
}

} // namespace QcBundledMods
