#pragma once

#include "QcBlockEntityRecord.h"
#include "QcContentRegistry.h"
#include "QcResult.h"

#include <QByteArray>
#include <QDataStream>
#include <QHash>
#include <QList>
#include <QString>

#include <memory>

// The block entities of a world directory, per chunk in one file per region
// (blockentities/r.<x>.<z>.qcb, the regions of QcRegionStorage; decision E-358). Each file holds the
// records of the chunks of its region that have anything to keep: containers and machines with
// their contents, saplings, what circuit components remember, scheduled ticks and positions the
// circuits wait to look at. Items and blocks are stored by name. Only regions whose records changed
// since the last write are written again, first all under staging names, then renamed; a region
// without records loses its file. No folder is made for a world without block entities.
class QcBlockEntityStorage
{
public:
    static constexpr auto folder = "blockentities";

    QcBlockEntityStorage(QString directory, std::shared_ptr<const QcContentRegistry> content);

    // Every record of every region file. Unknown items are dropped with a warning; a damaged file
    // is an error (contents must not get lost unnoticed).
    QcResult<QList<QcBlockEntityRecord>> read(QStringList &warnings) const;
    // Writes the regions whose records differ from what was read or written last (all of them the
    // first time); removes the files of regions that have no records any more.
    QcStatus write(const QList<QcBlockEntityRecord> &records) const;

private:
    QString regionPath(QcChunkPos region) const;
    // Writes the changed regions under staging names, then renames them all.
    QcStatus writeStaged(const QHash<QcChunkPos, QByteArray> &changed) const;
    QcStatus removeRegionsWithout(const QHash<QcChunkPos, QList<const QcBlockEntityRecord *>> &regions) const;
    QByteArray payloadOf(const QList<const QcBlockEntityRecord *> &records) const;
    void writeRecord(QDataStream &stream, const QcBlockEntityRecord &record) const;
    QcStatus readRegion(const QString &path, QList<QcBlockEntityRecord> &records, QStringList &warnings) const;

    QString m_directory;
    std::shared_ptr<const QcContentRegistry> m_content;
    // The payload of every region file as last read or written.
    mutable QHash<QcChunkPos, QByteArray> m_written;
};
