#pragma once

#include "QcContentRegistry.h"
#include "QcEntityRecord.h"
#include "QcRegionSideFiles.h"
#include "QcResult.h"

#include <QByteArray>
#include <QDataStream>
#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

#include <memory>

// The entities of a world directory, per chunk in one file per region (entities/r.<x>.<z>.qen, the
// regions of QcRegionStorage; decision E-366): each file holds the records of the chunks of its
// region with entities to keep (persistent creatures, dropped items and waiting drops, falling
// blocks, armour stands) and which of its chunks already got their animals. Items, blocks,
// creatures, armour and capes are stored by name. The files are QcRegionSideFiles: only changed
// regions are written, staged and then renamed.
class QcEntityStorage
{
public:
    static constexpr auto folder = "entities";
    // Entries of one kind in one chunk at most; a valid world never comes close.
    static constexpr quint32 maxPerChunk = 65536;

    QcEntityStorage(QString directory, std::shared_ptr<const QcContentRegistry> content);

    // Every record of every region file. Unknown items, blocks, creatures and capes and armour that
    // no longer fits its slot are dropped with a warning; a damaged or implausible file (an entity
    // outside its chunk, a chunk outside its region, values that cannot be) is an error, so no
    // entity gets lost unnoticed.
    QcResult<QcWorldEntityRecords> read(QStringList &warnings) const;
    // Writes the regions whose records changed since the last read or write; removes the files of
    // regions that have neither entities nor populated chunks any more.
    QcStatus write(const QcWorldEntityRecords &entities) const;

    // The file the last read() failed on, relative to the world directory; empty after a good read.
    QString damagedFile() const { return m_files.damagedFile(); }

private:
    QByteArray payloadOf(const QList<const QcEntityRecord *> &records, QList<QcChunkPos> populated) const;
    void writeRecord(QDataStream &stream, const QcEntityRecord &record) const;
    // `unknown` holds the unknown names reported so far in this read (one warning per world and name).
    QcStatus readRegion(const QString &path, const QByteArray &payload, QcWorldEntityRecords &entities, QStringList &warnings,
                        QSet<QString> &unknown) const;

    std::shared_ptr<const QcContentRegistry> m_content;
    QcRegionSideFiles m_files;
};
