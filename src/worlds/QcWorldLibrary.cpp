#include "worlds/QcWorldLibrary.h"

#include "common/core/QcDataRoot.h"
#include "server/storage/QcWorldStorage.h"

#include <QDir>
#include <QFileInfo>

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

QString QcWorldLibrary::dataRoot()
{
    return QcDataRoot::path();
}

QString QcWorldLibrary::defaultRoot()
{
    return QDir(dataRoot()).filePath(u"worlds"_s);
}

qint64 QcWorldLibrary::seedFromText(const QString &text)
{
    bool ok = false;
    const qint64 number = text.trimmed().toLongLong(&ok);
    if (ok) {
        return number;
    }
    quint64 hash = 14695981039346656037ull;
    for (const QChar c : text) {
        hash ^= c.unicode();
        hash *= 1099511628211ull;
    }
    return static_cast<qint64>(hash);
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
        const QcWorldStorage storage(entry.directory, nullptr);
        const int version = storage.storedFormatVersion();
        entry.tooOld = version > 0 && version < QcWorldInfo::oldestFormatVersion;
        const QcResult<QcWorldInfo> info = storage.readInfo();
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

QcResult<QcWorldLibrary::Entry> QcWorldLibrary::create(const QString &name, qint64 seed, const NewWorld &setup) const
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
    entry.info.keepInventory = setup.keepInventory;
    entry.info.generator = setup.generator;
    entry.info.generatorOptions = setup.options;
    entry.info.worldOptions = setup.worldOptions;
    entry.info.defaultGameMode = setup.gameMode;
    entry.info.setupDone = false;
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
