#include "QcWorldLibrary.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

QString folderNameFor(const QString &worldName)
{
    QString folder;
    const QString trimmed = worldName.trimmed();
    for (const QChar c : trimmed) {
        folder.append(c.isLetterOrNumber() || c == u'-' || c == u'_' ? c : u'_');
    }
    return folder.isEmpty() ? u"world"_s : folder.left(48);
}

bool isPlainFolderName(const QString &folder)
{
    return !folder.isEmpty() && folder != u"."_s && folder != u".."_s && !folder.contains(u'/') && !folder.contains(u'\\')
           && !folder.contains(u':');
}

} // namespace

QcWorldLibrary::QcWorldLibrary(QString root)
    : m_root(std::move(root))
{
}

QString QcWorldLibrary::defaultRoot()
{
    const QString overridden = qEnvironmentVariable("QTCRAFT_DATA_DIR");
    const QString base = overridden.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : overridden;
    return QDir(base).filePath(u"worlds"_s);
}

QString QcWorldLibrary::directoryOf(const QString &folder) const
{
    return QDir(m_root).filePath(folder);
}

QList<QcWorldLibrary::Entry> QcWorldLibrary::list() const
{
    QList<Entry> entries;
    const QDir root(m_root);
    const QStringList folders = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &folder : folders) {
        if (!QFile::exists(root.filePath(folder + u"/world.json"_s))) {
            continue;
        }
        Entry entry;
        entry.folder = folder;
        entry.directory = root.filePath(folder);
        const QcResult<QcWorldInfo> info = QcWorldStorage(entry.directory, nullptr).readInfo();
        if (info) {
            entry.info = *info;
        } else {
            entry.info.name = folder;
            // The message names the full path; the list shows the reason only.
            entry.error = QString(info.error()).replace(QDir(entry.directory).filePath(u"world.json"_s), u"world.json"_s);
        }
        entries.append(entry);
    }
    std::ranges::sort(entries, [](const Entry &a, const Entry &b) { return a.info.lastPlayed > b.info.lastPlayed; });
    return entries;
}

QcResult<QcWorldLibrary::Entry> QcWorldLibrary::create(const QString &name, qint64 seed) const
{
    const QString base = folderNameFor(name);
    QString folder = base;
    for (int suffix = 2; QDir(directoryOf(folder)).exists(); ++suffix) {
        folder = u"%1_%2"_s.arg(base).arg(suffix);
    }
    Entry entry;
    entry.folder = folder;
    entry.directory = directoryOf(folder);
    entry.info.name = name.trimmed().isEmpty() ? folder : name.trimmed();
    entry.info.seed = seed;
    entry.info.created = QDateTime::currentDateTimeUtc();
    entry.info.lastPlayed = entry.info.created;
    if (const QcStatus written = QcWorldStorage(entry.directory, nullptr).writeInfo(entry.info); !written) {
        return qcError(written.error());
    }
    return entry;
}

QcStatus QcWorldLibrary::remove(const QString &folder) const
{
    if (!isPlainFolderName(folder)) {
        return qcError(u"Invalid world folder '%1'"_s.arg(folder));
    }
    if (QFileInfo(directoryOf(folder)).isSymLink()) {
        return qcError(u"'%1' is a link, not a world folder"_s.arg(folder));
    }
    QDir directory(directoryOf(folder));
    if (!directory.exists(u"world.json"_s)) {
        return qcError(u"'%1' is not a QtCraft world"_s.arg(folder));
    }
    if (!directory.removeRecursively()) {
        return qcError(u"Cannot delete %1"_s.arg(directory.path()));
    }
    return {};
}
