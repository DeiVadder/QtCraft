#pragma once

#include "QcBlockEntityRecord.h"
#include "QcContentRegistry.h"
#include "QcRegionSideFiles.h"
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
// circuits wait to look at. Items and blocks are stored by name. The files are QcRegionSideFiles:
// only changed regions are written, staged and then renamed.
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

    // The file the last read() failed on, relative to the world directory; empty after a good read.
    QString damagedFile() const { return m_files.damagedFile(); }

private:
    QByteArray payloadOf(const QList<const QcBlockEntityRecord *> &records) const;
    void writeRecord(QDataStream &stream, const QcBlockEntityRecord &record) const;
    QcStatus readRegion(const QString &path, const QByteArray &payload, QList<QcBlockEntityRecord> &records, QStringList &warnings) const;

    std::shared_ptr<const QcContentRegistry> m_content;
    QcRegionSideFiles m_files;
};
