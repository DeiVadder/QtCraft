#pragma once

#include "QcChunkPos.h"
#include "QcResult.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <utility>

// Files that keep per-chunk data of a world beside the region files of the blocks, one file per
// region (the regions of QcRegionStorage) in a folder of the world: block entities
// (blockentities/r.<x>.<z>.qcb, decision E-358) and entities (entities/r.<x>.<z>.qen, decision
// E-366). Each file is a QcSaveFile with the payload its storage makes. Only regions whose payload
// changed since the last read or write are written again, all of them first under a staging name
// (`.next`) and then renamed, so a save that breaks off while writing leaves the previous files
// whole; regions without a payload lose their file. No folder is made while there is nothing to keep.
class QcRegionSideFiles
{
public:
    QcRegionSideFiles(QString directory, QString folder, QString extension, QByteArrayView magic, quint16 version);

    QString regionPath(QcChunkPos region) const;
    // Writes the regions whose payload differs from the last one read or written, removes the files
    // of all regions missing in `payloads` (also files never read).
    QcStatus write(const QHash<QcChunkPos, QByteArray> &payloads) const;
    // The payload of every file of the folder, with its path, in name order; a damaged or foreign
    // file is an error. Forgets what was written before.
    QcResult<QList<std::pair<QString, QByteArray>>> read() const;
    // A payload read from the file of `region` was valid: it counts as written.
    void remember(QcChunkPos region, const QByteArray &payload) const { m_written.insert(region, payload); }
    // Whether `path` is the file of `region`.
    bool isFileOf(const QString &path, QcChunkPos region) const;
    // The file the last read() failed on (damaged, of an older version or with implausible contents
    // noted by noteDamaged), relative to the world directory; empty when it succeeded.
    QString damagedFile() const { return m_damaged; }
    void noteDamaged(const QString &path) const;

private:
    QString folderPath() const;
    // `files`: the folder's files before the write.
    QcStatus writeStaged(const QHash<QcChunkPos, QByteArray> &changed, const QStringList &files) const;
    QcStatus removeRegionsWithout(const QHash<QcChunkPos, QByteArray> &payloads, const QStringList &files) const;

    QString m_directory;
    QString m_folder;
    QString m_extension;
    QByteArray m_magic;
    quint16 m_version = 1;
    // The payload of every region file as last read or written.
    mutable QHash<QcChunkPos, QByteArray> m_written;
    mutable QString m_damaged;
};
