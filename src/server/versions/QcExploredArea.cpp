// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "server/versions/QcExploredArea.h"

#include "common/content/worldgen/QcDimensionDefinition.h"
#include "common/maps/QcMapData.h"
#include "common/maps/QcMapPixel.h"
#include "server/maps/QcMapSaveCodec.h"
#include "server/storage/QcChunkStore.h"
#include "server/storage/QcGeneratedChunks.h"
#include "server/storage/QcWorldStorage.h"

#include <QDir>
#include <QFile>

using namespace Qt::StringLiterals;

namespace {

// The copies of a damaged index of generated chunks kept aside in `directory` (QcSaveFile::keepAside).
QStringList damagedIndexCopies(const QString &directory)
{
    return QDir(directory).entryList({QcGeneratedChunks::fileName.toString() + u".damaged*"_s}, QDir::Files, QDir::Name);
}

// The chunks the store in `directory` knows: stored ones and the index of generated chunks.
QcResult<QcChunkSet> storedChunks(const QString &directory)
{
    QcChunkStore store(directory);
    if (const QcStatus opened = store.open(); !opened) {
        return qcError(opened.error());
    }
    if (store.generatedFile() != QcGeneratedChunks::fileName) {
        return qcError(u"The index of generated chunks in %1 was written by a newer version of QtCraft"_s.arg(directory));
    }
    // A damaged index (kept aside now or by an earlier start) lost chunks the area must hold.
    if (!store.openWarnings().isEmpty()) {
        return qcError(store.openWarnings().join(u"; "_s));
    }
    if (const QStringList copies = damagedIndexCopies(directory); !copies.isEmpty()) {
        return qcError(u"The index of generated chunks in %1 was damaged and kept aside as %2, so the explored area is not known in full; "
                       u"removing the copies switches with what is known"_s.arg(directory, copies.join(u", "_s)));
    }
    QcChunkSet chunks = store.generated().chunkSet();
    const QHash<QcChunkPos, QcChunkStore::Stored> catalog = store.catalog();
    for (auto it = catalog.cbegin(); it != catalog.cend(); ++it) {
        chunks.insert(it.key());
    }
    return chunks;
}

// The chunks under `pixel` of `map` (one at scale 4 and below: map squares lie on chunk borders).
QcChunkSet chunksUnder(const QcMapData &map, QPoint pixel)
{
    QcChunkSet chunks;
    const QPoint first = map.columnOf(pixel);
    const int width = map.blocksPerPixel();
    const QcChunkPos low = QcBlockPos{first.x(), 0, first.y()}.chunk();
    const QcChunkPos high = QcBlockPos{first.x() + width - 1, 0, first.y() + width - 1}.chunk();
    for (int x = low.x; x <= high.x; ++x) {
        for (int z = low.z; z <= high.z; ++z) {
            if (QcChunkPos{x, z}.isInsideWorld()) {
                chunks.insert({x, z});
            }
        }
    }
    return chunks;
}

// The chunks under the revealed pixels of `map`.
QcChunkSet chunksOf(const QcMapData &map)
{
    QcChunkSet chunks;
    for (int row = 0; row < QcMapData::size; ++row) {
        for (int column = 0; column < QcMapData::size; ++column) {
            if (map.at({column, row}) != QcMapPixel::blank) {
                chunks.unite(chunksUnder(map, {column, row}));
            }
        }
    }
    return chunks;
}

} // namespace

namespace QcExploredArea {

QcResult<Explored> of(const QString &worldDirectory, const QcDimensionDefinition &dimension, const QcContentRegistry &content)
{
    // The journal of the last save lies in the world directory and names the files of every
    // dimension: finished first, before a dimension's store removes staged files it still needs.
    QcResult<QcChunkSet> world = storedChunks(worldDirectory);
    if (!world) {
        return qcError(world.error());
    }
    if (!dimension.isOverworld()) {
        const QString directory = QcWorldStorage::dimensionDirectory(worldDirectory, dimension);
        QcResult<QcChunkSet> own = QDir(directory).exists() ? storedChunks(directory) : QcResult<QcChunkSet>(QcChunkSet{});
        if (!own) {
            return qcError(own.error());
        }
        return Explored{*own, {}};
    }

    Explored explored = ofMaps(worldDirectory, content);
    explored.chunks.unite(*world);
    return explored;
}

Explored ofMaps(const QString &worldDirectory, const QcContentRegistry &content)
{
    Explored explored;
    const QDir folder(QDir(worldDirectory).filePath(u"maps"_s));
    const QStringList names = folder.entryList({u"*.qcmap"_s}, QDir::Files, QDir::Name);
    for (const QString &name : names) {
        QFile file(folder.filePath(name));
        const QByteArray bytes = file.open(QIODevice::ReadOnly) ? file.read(QcMapSaveCodec::maxBytes + 1) : QByteArray();
        const QcResult<QcMapData> map = QcMapSaveCodec::read(bytes, file.fileName(), content);
        if (!map) {
            explored.warnings.append(u"%1; the area it shows does not count as explored"_s.arg(map.error()));
            continue;
        }
        explored.chunks.unite(chunksOf(*map));
    }
    return explored;
}

} // namespace QcExploredArea
