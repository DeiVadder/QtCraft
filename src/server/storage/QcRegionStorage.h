#pragma once

#include "QcChunk.h"
#include "QcContentRegistry.h"
#include "QcResult.h"

#include <QDataStream>
#include <QHash>
#include <QList>
#include <QString>

#include <memory>

// Modified chunks of a world in region files (regions/r.<x>.<z>.qcr, regionSize x regionSize
// chunks each). Each file has its own palette of block state names, so content changes between
// sessions do not scramble a world.
class QcRegionStorage
{
public:
    static constexpr int regionSize = 32;
    using Chunks = QHash<QcChunkPos, std::shared_ptr<QcChunk>>;

    QcRegionStorage(QString directory, std::shared_ptr<const QcContentRegistry> content);

    static QcChunkPos regionOf(QcChunkPos chunk);

    // Loads every stored chunk. Unknown block names become air and are listed in `warnings`.
    QcResult<Chunks> read(QStringList &warnings) const;
    // Rewrites the region files that contain any of the given chunks with all chunks of those regions.
    QcStatus write(const QHash<QcChunkPos, std::shared_ptr<const QcChunk>> &chunks) const;

private:
    QString regionPath(QcChunkPos region) const;
    QcStatus writeRegion(QcChunkPos region, const QList<std::shared_ptr<const QcChunk>> &chunks) const;
    // Copies `chunk` into `copy` with ids of the file-local palette `names`, extending it.
    QcStatus toLocalIds(const QcChunk &chunk, QcChunk &copy, QHash<QcBlockId, QcBlockId> &localIds, QList<QString> &names) const;
    QcStatus readRegion(const QString &path, Chunks &chunks, QStringList &warnings) const;
    // The runtime ids of a region file's palette.
    QcResult<QList<QcBlockId>> readPalette(QDataStream &stream, const QString &path, QStringList &warnings) const;
    QcStatus readChunk(QDataStream &stream, const QString &path, const QList<QcBlockId> &runtimeIds, Chunks &chunks) const;

    QString m_directory;
    std::shared_ptr<const QcContentRegistry> m_content;
};
