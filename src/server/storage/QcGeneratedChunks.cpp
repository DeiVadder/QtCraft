// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/storage/QcGeneratedChunks.h"

#include "server/storage/QcRegionFile.h"
#include "server/storage/QcSaveFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <limits>

using namespace Qt::StringLiterals;

namespace {

static_assert(QcChunkSet::regionSize == QcRegionFile::regionSize, "the index counts regions like the region files");
static_assert(QcChunkSet::streamVersion == QcSaveFile::streamVersion, "chunk sets are written like every save file");

// Whether every name QcSaveFile::keepAside tries for `path` is taken.
bool asideCopiesTaken(const QString &path)
{
    for (int attempt = 1; attempt <= QcSaveFile::maxDamagedCopies; ++attempt) {
        if (!QFile::exists(attempt == 1 ? path + u".damaged"_s : u"%1.damaged.%2"_s.arg(path).arg(attempt))) {
            return false;
        }
    }
    return true;
}

// Keeps the damaged file `path` aside like the villages (QcServerPersistence::restoreVillages):
// the original goes only once its copy exists. Returns the warning for the player.
QString keptAside(const QString &path, const QString &error)
{
    const QString start = u"%1; the index of generated chunks starts empty"_s.arg(error);
    const QString copy = QcSaveFile::keepAside(path);
    if (copy.isEmpty()) {
        if (asideCopiesTaken(path)) {
            return u"%1, and all %2 copies kept aside (%3.damaged ...) are taken: the next save replaces the damaged file"_s.arg(
                start).arg(QcSaveFile::maxDamagedCopies).arg(QFileInfo(path).fileName());
        }
        return u"%1, and the file could not be kept aside: the next save replaces it"_s.arg(start);
    }
    QFile::remove(path);
    return u"%1, the damaged file was kept as %2"_s.arg(start, QFileInfo(copy).fileName());
}

} // namespace

void QcGeneratedChunks::note(QcChunkPos chunk)
{
    if (m_chunks.insert(chunk)) {
        setUnsaved(true);
    }
}

QcGeneratedChunks::Opened QcGeneratedChunks::open(const QString &directory)
{
    Opened opened = load(directory, fileName.toString());
    if (!opened.newer) {
        return opened;
    }
    // A newer build's index stays as it is; what this build adds goes to a file of its own format
    // (decision E-946), which a newer build takes into its index.
    Opened older = load(directory, olderFileName.toString());
    older.newer = true;
    older.warnings = opened.warnings + older.warnings;
    return older;
}

QcGeneratedChunks::Opened QcGeneratedChunks::load(const QString &directory, const QString &name)
{
    Opened opened;
    opened.file = name;
    const QString path = QDir(directory).filePath(name);
    if (!QFile::exists(path)) {
        return opened;
    }
    // Any version passes the container check here, so a newer one is told apart from damage.
    const QcResult<QcSaveFile::Payload> payload = QcSaveFile::read(path, magic, std::numeric_limits<quint16>::max());
    if (payload && payload->version > formatVersion) {
        opened.newer = true;
        opened.warnings.append(u"%1 has format version %2, newer than this version of QtCraft reads (%3): it is left as it is, and the chunks "
                               u"generated now are kept in %4"_s.arg(path)
                                   .arg(payload->version)
                                   .arg(formatVersion)
                                   .arg(olderFileName));
        return opened;
    }
    const QcResult<QcGeneratedChunks> read = payload ? parse(payload->bytes, path) : QcResult<QcGeneratedChunks>(qcError(payload.error()));
    if (!read) {
        opened.warnings.append(keptAside(path, read.error()));
        return opened;
    }
    opened.index = *read;
    return opened;
}

QByteArray QcGeneratedChunks::bytes() const
{
    return QcSaveFile::bytes(magic, m_chunks.bytes(), formatVersion);
}

QcResult<QcGeneratedChunks> QcGeneratedChunks::parse(const QByteArray &payload, const QString &path)
{
    QcResult<QcChunkSet> chunks = QcChunkSet::parse(payload);
    if (!chunks) {
        return qcError(u"%1 is damaged (%2)"_s.arg(path, chunks.error()));
    }
    QcGeneratedChunks index;
    index.m_chunks = std::move(*chunks);
    return index;
}
