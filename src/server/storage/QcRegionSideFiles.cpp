#include "QcRegionSideFiles.h"

#include "QcSaveFile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <filesystem>
#include <system_error>

using namespace Qt::StringLiterals;

QcRegionSideFiles::QcRegionSideFiles(QString directory, QString folder, QString extension, QByteArrayView magic, quint16 version)
    : m_directory(std::move(directory))
    , m_folder(std::move(folder))
    , m_extension(std::move(extension))
    , m_magic(magic.toByteArray())
    , m_version(version)
{
}

QString QcRegionSideFiles::folderPath() const
{
    return QDir(m_directory).filePath(m_folder);
}

QString QcRegionSideFiles::regionPath(QcChunkPos region) const
{
    return QDir(folderPath()).filePath(u"r.%1.%2.%3"_s.arg(region.x).arg(region.z).arg(m_extension));
}

void QcRegionSideFiles::noteDamaged(const QString &path) const
{
    m_damaged = QDir(m_directory).relativeFilePath(path);
}

bool QcRegionSideFiles::isFileOf(const QString &path, QcChunkPos region) const
{
    return QFileInfo(regionPath(region)).fileName() == QFileInfo(path).fileName();
}

QcStatus QcRegionSideFiles::write(const QHash<QcChunkPos, QByteArray> &payloads) const
{
    if (payloads.isEmpty() && !QDir(folderPath()).exists()) {
        return {};
    }
    if (!QDir().mkpath(folderPath())) {
        return qcError(u"Cannot create the folder %1 in %2"_s.arg(m_folder, m_directory));
    }

    QHash<QcChunkPos, QByteArray> changed;
    for (auto it = payloads.cbegin(); it != payloads.cend(); ++it) {
        if (m_written.value(it.key()) != it.value()) {
            changed.insert(it.key(), it.value());
        }
    }

    // One listing of the folder serves both the leftovers of a broken save and the regions to remove.
    const QStringList files = QDir(folderPath()).entryList({u"r.*"_s}, QDir::Files);
    if (const QcStatus written = writeStaged(changed, files); !written) {
        return written;
    }
    return removeRegionsWithout(payloads, files);
}

QcStatus QcRegionSideFiles::writeStaged(const QHash<QcChunkPos, QByteArray> &changed, const QStringList &files) const
{
    // All changed regions are written under a staging name first and only then renamed, so a save
    // that breaks off while writing leaves the previous files whole (decision E-358).
    const QDir folder(folderPath());
    const QString staging = u".%1.next"_s.arg(m_extension);
    for (const QString &fileName : files) {
        if (fileName.endsWith(staging)) {
            QFile::remove(folder.filePath(fileName));
        }
    }
    for (auto it = changed.constBegin(); it != changed.constEnd(); ++it) {
        if (const QcStatus written = QcSaveFile::write(regionPath(it.key()) + u".next"_s, m_magic, it.value(), m_version); !written) {
            return written;
        }
    }
    for (auto it = changed.constBegin(); it != changed.constEnd(); ++it) {
        const QString target = regionPath(it.key());
        std::error_code error;
        std::filesystem::rename(std::filesystem::path(QFile::encodeName(target + u".next"_s).toStdString()),
                                std::filesystem::path(QFile::encodeName(target).toStdString()), error);
        if (error) {
            return qcError(u"Cannot replace %1: %2"_s.arg(target, QString::fromStdString(error.message())));
        }
        m_written.insert(it.key(), it.value());
    }
    return {};
}

QcStatus QcRegionSideFiles::removeRegionsWithout(const QHash<QcChunkPos, QByteArray> &payloads, const QStringList &files) const
{
    // Every file of a region without a payload goes, also files this storage never read.
    const QDir folder(folderPath());
    const QString extension = u"."_s + m_extension;
    for (const QString &fileName : files) {
        if (!fileName.endsWith(extension)) {
            continue;
        }
        const QStringList parts = fileName.split(u'.');
        bool xOk = false;
        bool zOk = false;
        const QcChunkPos region{parts.value(1).toInt(&xOk), parts.value(2).toInt(&zOk)};
        if (xOk && zOk && parts.size() == 4 && payloads.contains(region)) {
            continue;
        }
        if (!QFile::remove(folder.filePath(fileName))) {
            return qcError(u"Cannot remove %1"_s.arg(folder.filePath(fileName)));
        }
        m_written.remove(region);
    }
    return {};
}

QcResult<QList<std::pair<QString, QByteArray>>> QcRegionSideFiles::read() const
{
    m_written.clear();
    m_damaged.clear();
    QList<std::pair<QString, QByteArray>> files;
    const QDir folder(folderPath());
    const QStringList names = folder.entryList({u"r.*.%1"_s.arg(m_extension)}, QDir::Files, QDir::Name);
    for (const QString &fileName : names) {
        const QString path = folder.filePath(fileName);
        quint16 version = 0;
        const QcResult<QByteArray> payload = QcSaveFile::read(path, m_magic, m_version, &version);
        if (!payload) {
            noteDamaged(path);
            return qcError(payload.error());
        }
        if (version < m_version) {
            noteDamaged(path);
            return qcError(u"%1 is from an older development version"_s.arg(path));
        }
        files.append({path, *payload});
    }
    return files;
}
