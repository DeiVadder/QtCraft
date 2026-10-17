#include "server/storage/QcChunkStore.h"

#include "common/core/QcCrc32.h"
#include "common/protocol/QcStreamLimits.h"
#include "server/storage/QcSaveFile.h"

#include <QDataStream>
#include <QIODevice>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>
#include <filesystem>
#include <system_error>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <unistd.h>
#endif

using namespace Qt::StringLiterals;

namespace {

struct KindFiles
{
    const char *folder = nullptr;
    const char *extension = nullptr;
    QByteArrayView magic;
    quint16 version = 0;
};

// Indexed region files (decision E-373): blocks version 3, block entities 4 (signal waits apart, fires), entities 3; the
// files of older development versions are refused (decision E-356).
constexpr std::array<KindFiles, QcChunkStore::kindCount> kinds{{
    {"regions", "qcr", "QCRG", 3},
    {"blockentities", "qcb", "QCBE", 4},
    {"entities", "qen", "QCER", 3},
}};

constexpr QByteArrayView journalMagic("QCSJ");
constexpr quint32 maxJournalSteps = 1u << 20;
// Version 2: steps with size and checksum of the head of their file.
constexpr quint16 journalVersion = 2;
// The head of a file a step checks: a region's header and index (whose entries hold the checksum of
// every blob), or all of a small file.
constexpr qint64 headBytes = qint64(64) * 1024;

const KindFiles &filesOf(QcChunkStore::Kind kind)
{
    return kinds[std::size_t(kind)];
}

// Makes renames and removals in `directory` durable (a rename is in the folder's entry, not in the
// file). Without POSIX folders nothing is done; the renames themselves are atomic there too.
void syncDirectory(const QString &directory)
{
#ifdef Q_OS_UNIX
    const int fd = ::open(QFile::encodeName(directory).constData(), O_RDONLY | O_DIRECTORY);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
#else
    Q_UNUSED(directory);
#endif
}

[[nodiscard]] QcStatus renameFile(const QString &from, const QString &to)
{
    // filesystemFilePath() keeps every character of the path on every system (a narrow string would
    // go through the ANSI code page on Windows).
    std::error_code error;
    std::filesystem::rename(QFileInfo(from).filesystemFilePath(), QFileInfo(to).filesystemFilePath(), error);
    if (error) {
        return qcError(u"Cannot replace %1: %2"_s.arg(to, QString::fromStdString(error.message())));
    }
    return {};
}

} // namespace

QcChunkStore::QcChunkStore(QString directory)
    : m_directory(std::move(directory))
{
}

QString QcChunkStore::folderOf(Kind kind)
{
    return QString::fromLatin1(filesOf(kind).folder);
}

QString QcChunkStore::extensionOf(Kind kind)
{
    return QString::fromLatin1(filesOf(kind).extension);
}

QString QcChunkStore::absolute(const QString &name) const
{
    return QDir(m_directory).filePath(name);
}

QcStatus QcChunkStore::open()
{
    m_damaged.reset();
    for (auto &index : m_indexes) {
        index.clear();
    }
    if (m_directory.isEmpty()) {
        return {};
    }
    if (const QcStatus finished = finishJournal(); !finished) {
        m_damaged = Damaged{Kind::Blocks, journalName.toString(), true};
        return finished;
    }
    removeStaged();
    return readIndexes();
}

QcStatus QcChunkStore::finishJournal()
{
    const QString path = absolute(journalName.toString());
    if (!QFile::exists(path)) {
        return {};
    }

    // A journal on disk is complete (it is written whole before any rename): its save goes on.
    const QcResult<QByteArray> payload = QcSaveFile::read(path, journalMagic, journalVersion);
    if (!payload) {
        return qcError(u"%1; the last save of this world broke off and cannot be finished"_s.arg(payload.error()));
    }
    QDataStream stream(*payload);
    stream.setVersion(QcSaveFile::streamVersion);
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, maxJournalSteps, count)) {
        return qcError(u"%1 is damaged"_s.arg(path));
    }
    QList<Step> steps;
    for (quint32 i = 0; i < count; ++i) {
        Step step;
        stream >> step.name >> step.remove >> step.size >> step.headCrc;
        if (stream.status() != QDataStream::Ok || step.name.contains(u"..") || QDir::isAbsolutePath(step.name)) {
            return qcError(u"%1 is damaged"_s.arg(path));
        }
        steps.append(step);
    }
    if (const QcStatus done = runSteps(steps); !done) {
        return done;
    }
    QFile::remove(path);
    syncDirectory(m_directory);
    return {};
}

void QcChunkStore::removeStaged() const
{
    QStringList folders{m_directory, absolute(u"players"_s)};
    for (int kind = 0; kind < kindCount; ++kind) {
        folders.append(absolute(folderOf(Kind(kind))));
    }
    for (const QString &folder : std::as_const(folders)) {
        const QDir dir(folder);
        const QStringList staged = dir.entryList({u"*.next"_s}, QDir::Files);
        for (const QString &name : staged) {
            QFile::remove(dir.filePath(name));
        }
    }
}

QcStatus QcChunkStore::readIndexes()
{
    for (int k = 0; k < kindCount; ++k) {
        const Kind kind = Kind(k);
        const QDir folder(absolute(folderOf(kind)));
        const QStringList names = folder.entryList({u"r.*.%1"_s.arg(extensionOf(kind))}, QDir::Files, QDir::Name);
        for (const QString &name : names) {
            const std::optional<QcChunkPos> region = QcRegionFile::regionOfFile(name, extensionOf(kind));
            const QcResult<QcRegionFile::Index> index = region ? QcRegionFile::readIndex(folder.filePath(name), *region, filesOf(kind).magic,
                                                                                        filesOf(kind).version)
                                                               : QcResult<QcRegionFile::Index>(qcError(u"%1 is not a region file"_s.arg(folder.filePath(name))));
            if (!index || !region) {
                m_damaged = Damaged{kind, QDir(m_directory).relativeFilePath(folder.filePath(name)), false};
                return qcError(index.error());
            }
            if (!index->isEmpty()) {
                m_indexes[std::size_t(k)].insert(*region, *index);
            }
        }
    }
    return {};
}

void QcChunkStore::addToCatalog(Kind kind, QHash<QcChunkPos, Stored> &catalog) const
{
    const auto note = [&catalog, kind](QcChunkPos chunk, quint16 marks) {
        Stored &stored = catalog[chunk];
        stored.kinds |= quint8(1u << quint8(kind));
        if (kind == Kind::BlockEntities) {
            stored.marks = marks;
        }
    };
    const QHash<QcChunkPos, Pending> &pending = m_pending[std::size_t(kind)];
    for (const QcRegionFile::Index &index : m_indexes[std::size_t(kind)]) {
        for (auto it = index.cbegin(); it != index.cend(); ++it) {
            if (!pending.contains(it.key())) {
                note(it.key(), it->marks);
            }
        }
    }
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        if (it->blob) {
            note(it.key(), it->marks);
        }
    }
}

QHash<QcChunkPos, QcChunkStore::Stored> QcChunkStore::catalog() const
{
    QHash<QcChunkPos, Stored> catalog;
    for (int k = 0; k < kindCount; ++k) {
        addToCatalog(Kind(k), catalog);
    }
    return catalog;
}

QcResult<QcChunkStore::Blobs> QcChunkStore::read(QcChunkPos chunk)
{
    if (const QcStatus reopened = reopenIfNeeded(); !reopened) {
        return qcError(reopened.error());
    }
    Blobs blobs;
    for (int k = 0; k < kindCount; ++k) {
        const Kind kind = Kind(k);
        auto &slot = blobs.data[std::size_t(k)];
        if (const auto pending = m_pending[std::size_t(k)].constFind(chunk); pending != m_pending[std::size_t(k)].cend()) {
            slot = pending->blob;
            if (kind == Kind::BlockEntities) {
                blobs.marks = pending->marks;
            }
            continue;
        }

        const QcChunkPos region = QcRegionFile::regionOf(chunk);
        const auto index = m_indexes[std::size_t(k)].constFind(region);
        if (index == m_indexes[std::size_t(k)].cend() || !index->contains(chunk)) {
            continue;
        }
        const QcRegionFile::Entry entry = index->value(chunk);
        QcResult<QByteArray> data = QcRegionFile::readBlob(QcRegionFile::path(absolute(folderOf(kind)), region, extensionOf(kind)), entry);
        if (!data) {
            return qcError(data.error());
        }
        slot = std::move(*data);
        if (kind == Kind::BlockEntities) {
            blobs.marks = entry.marks;
        }
    }
    return blobs;
}

bool QcChunkStore::has(Kind kind, QcChunkPos chunk) const
{
    const std::size_t k = std::size_t(kind);
    if (const auto pending = m_pending[k].constFind(chunk); pending != m_pending[k].cend()) {
        return pending->blob.has_value();
    }
    const auto index = m_indexes[k].constFind(QcRegionFile::regionOf(chunk));
    return index != m_indexes[k].cend() && index->contains(chunk);
}

void QcChunkStore::put(Kind kind, QcChunkPos chunk, std::optional<QByteArray> blob, quint16 marks)
{
    m_pending[std::size_t(kind)].insert(chunk, Pending{std::move(blob), kind == Kind::BlockEntities ? marks : quint16(0)});
}

qsizetype QcChunkStore::pendingChunks() const
{
    qsizetype count = 0;
    for (const auto &pending : m_pending) {
        count = std::max(count, pending.size());
    }
    return count;
}

QcStatus QcChunkStore::stageRegion(Kind kind, QcChunkPos region, QList<Step> &steps, QHash<QcChunkPos, QcRegionFile::Index> &written)
{
    const std::size_t k = std::size_t(kind);
    const QString folder = absolute(folderOf(kind));
    const QString path = QcRegionFile::path(folder, region, extensionOf(kind));
    const QcRegionFile::Index old = m_indexes[k].value(region);

    // Whether anything changes, from the index alone: an unchanged region is not even read.
    bool changed = false;
    for (auto it = m_pending[k].cbegin(); it != m_pending[k].cend() && !changed; ++it) {
        if (QcRegionFile::regionOf(it.key()) != region) {
            continue;
        }
        const auto entry = old.constFind(it.key());
        if (!it->blob) {
            changed = entry != old.cend();
        } else {
            changed = entry == old.cend() || qsizetype(entry->size) != it->blob->size() || entry->crc != qcCrc32(*it->blob) || entry->marks != it->marks;
        }
    }
    if (!changed) {
        written.insert(region, old);
        return {};
    }

    // What stays: the blobs of the old file, as bytes, unless something new replaces them; the old
    // file is read only when some of its blobs stay.
    // Kept blobs keep their checksum: a damaged one stays damaged (decision E-373).
    const bool keepsOld = std::ranges::any_of(old.keys(), [&](QcChunkPos chunk) { return !m_pending[k].contains(chunk); });
    QHash<QcChunkPos, QcRegionFile::Blob> blobs;
    if (keepsOld) {
        QList<QcChunkPos> damaged;
        QcResult<QList<QcRegionFile::Blob>> read = QcRegionFile::readAll(path, old, &damaged);
        if (!read) {
            return qcError(read.error());
        }
        for (const QcRegionFile::Blob &blob : std::as_const(*read)) {
            blobs.insert(blob.chunk, blob);
        }
        for (const QcChunkPos &chunk : std::as_const(damaged)) {
            qWarning("%s: chunk %d, %d is damaged and kept as it is", qPrintable(path), chunk.x, chunk.z);
        }
    }
    for (auto it = m_pending[k].cbegin(); it != m_pending[k].cend(); ++it) {
        if (QcRegionFile::regionOf(it.key()) != region) {
            continue;
        }
        if (!it->blob) {
            blobs.remove(it.key());
        } else {
            blobs.insert(it.key(), {it.key(), it->marks, *it->blob, std::nullopt});
        }
    }

    const QString name = QDir(m_directory).relativeFilePath(path);
    if (blobs.isEmpty()) {
        steps.append({name, true, 0, 0});
        written.insert(region, {});
        return {};
    }
    if (!QDir().mkpath(folder)) {
        return qcError(u"Cannot create the folder %1"_s.arg(folder));
    }
    const QcResult<QcRegionFile::Index> index = QcRegionFile::write(path + u".next"_s, filesOf(kind).magic, filesOf(kind).version, blobs.values());
    if (!index) {
        return qcError(index.error());
    }
    steps.append({name, false, 0, 0});
    written.insert(region, *index);
    return {};
}

QcStatus QcChunkStore::writeJournal(const QList<Step> &steps) const
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QcSaveFile::streamVersion);
    stream << quint32(steps.size());
    for (const Step &step : steps) {
        stream << step.name << step.remove << step.size << step.headCrc;
    }
    const QcStatus written = QcSaveFile::write(absolute(journalName.toString()), journalMagic, payload, journalVersion);
    syncDirectory(m_directory);
    return written;
}

QcStatus QcChunkStore::stamp(const QString &path, Step &step)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return qcError(u"Cannot read %1 back: %2"_s.arg(path, file.errorString()));
    }
    step.size = file.size();
    step.headCrc = qcCrc32(file.read(headBytes));
    return {};
}

QcStatus QcChunkStore::runSteps(const QList<Step> &steps)
{
    for (const Step &step : steps) {
        const QString target = absolute(step.name);
        if (step.remove) {
            if (QFile::exists(target) && !QFile::remove(target)) {
                return qcError(u"Cannot remove %1"_s.arg(target));
            }
            continue;
        }
        // Finishing a journal again after a rename already happened finds no staged file; the file
        // in place must then be the one the step names, else the staged file was lost.
        if (!QFile::exists(target + u".next"_s)) {
            Step found;
            if (!QFile::exists(target) || !stamp(target, found) || found.size != step.size || found.headCrc != step.headCrc) {
                return qcError(u"%1.next is missing and %1 is not the file the save wrote"_s.arg(target));
            }
            continue;
        }
        if (m_renamesLeft == 0) {
            m_renamesLeft = -1;
            return qcError(u"Cannot replace %1: rename refused (test)"_s.arg(target));
        }
        m_renamesLeft = std::max(-1, m_renamesLeft - 1);
        if (const QcStatus renamed = renameFile(target + u".next"_s, target); !renamed) {
            return renamed;
        }
    }

    QSet<QString> folders;
    for (const Step &step : steps) {
        folders.insert(QFileInfo(absolute(step.name)).path());
    }
    for (const QString &folder : std::as_const(folders)) {
        syncDirectory(folder);
    }
    return {};
}

QcStatus QcChunkStore::stageAll(QList<Step> &steps, std::array<QHash<QcChunkPos, QcRegionFile::Index>, kindCount> &written)
{
    for (int k = 0; k < kindCount; ++k) {
        QSet<QcChunkPos> regions;
        for (auto it = m_pending[std::size_t(k)].cbegin(); it != m_pending[std::size_t(k)].cend(); ++it) {
            regions.insert(QcRegionFile::regionOf(it.key()));
        }
        for (const QcChunkPos &region : std::as_const(regions)) {
            if (const QcStatus staged = stageRegion(Kind(k), region, steps, written[std::size_t(k)]); !staged) {
                return staged;
            }
        }
    }
    for (auto file = m_pendingFiles.cbegin(); file != m_pendingFiles.cend(); ++file) {
        steps.append({file.key(), !file->has_value(), 0, 0});
        if (!file->has_value()) {
            continue;
        }
        if (!QDir().mkpath(QFileInfo(absolute(file.key())).path())) {
            return qcError(u"Cannot create the folder of %1"_s.arg(absolute(file.key())));
        }
        QSaveFile staged(absolute(file.key()) + u".next"_s);
        if (!staged.open(QIODevice::WriteOnly) || staged.write(**file) != (*file)->size() || !staged.commit()) {
            return qcError(u"Cannot write %1: %2"_s.arg(absolute(file.key()), staged.errorString()));
        }
    }
    for (Step &step : steps) {
        if (!step.remove) {
            if (const QcStatus stamped = stamp(absolute(step.name) + u".next"_s, step); !stamped) {
                return stamped;
            }
        }
    }
    return {};
}

QcStatus QcChunkStore::reopenIfNeeded()
{
    if (!m_needsReopen) {
        return {};
    }
    for (auto &index : m_indexes) {
        index.clear();
    }
    if (const QcStatus finished = finishJournal(); !finished) {
        return finished;
    }
    removeStaged();
    if (const QcStatus read = readIndexes(); !read) {
        return read;
    }
    m_needsReopen = false;
    return {};
}

QcStatus QcChunkStore::commit(const QList<WholeFile> &files)
{
    if (m_directory.isEmpty()) {
        return {};
    }
    if (const QcStatus reopened = reopenIfNeeded(); !reopened) {
        for (const WholeFile &file : files) {
            m_pendingFiles.insert(file.name, file.content);
        }
        return reopened;
    }
    if (!QDir().mkpath(m_directory)) {
        return qcError(u"Cannot create %1"_s.arg(m_directory));
    }

    for (const WholeFile &file : files) {
        m_pendingFiles.insert(file.name, file.content);
    }
    QList<Step> steps;
    std::array<QHash<QcChunkPos, QcRegionFile::Index>, kindCount> written;
    QcStatus result = stageAll(steps, written);
    // The journal makes the save happen: before it, the previous save is whole.
    if (result) {
        result = writeJournal(steps);
    }
    if (!result) {
        removeStaged();
        return result;
    }
    if (const QcStatus done = runSteps(steps); !done) {
        // Some files may be renamed already: what this store knows of them is stale. The journal
        // stays; before anything else is read or written, the save is finished from it and the
        // indexes are read again (reopen()), as opening the world would.
        m_needsReopen = true;
        return done;
    }
    QFile::remove(absolute(journalName.toString()));
    syncDirectory(m_directory);

    for (int k = 0; k < kindCount; ++k) {
        for (auto it = written[std::size_t(k)].cbegin(); it != written[std::size_t(k)].cend(); ++it) {
            if (it->isEmpty()) {
                m_indexes[std::size_t(k)].remove(it.key());
            } else {
                m_indexes[std::size_t(k)].insert(it.key(), it.value());
            }
        }
        m_pending[std::size_t(k)].clear();
    }
    m_pendingFiles.clear();
    return {};
}

QcResult<std::optional<QByteArray>> QcChunkStore::readFile(const QString &name)
{
    if (const auto pending = m_pendingFiles.constFind(name); pending != m_pendingFiles.cend()) {
        return *pending;
    }
    if (const QcStatus reopened = reopenIfNeeded(); !reopened) {
        return qcError(reopened.error());
    }
    QFile file(absolute(name));
    if (!file.exists()) {
        return std::optional<QByteArray>();
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return qcError(u"Cannot open %1: %2"_s.arg(file.fileName(), file.errorString()));
    }
    return std::optional<QByteArray>(file.read(qint64(64) * 1024 * 1024));
}
