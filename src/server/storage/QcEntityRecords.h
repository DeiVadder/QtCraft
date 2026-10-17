#pragma once

#include "server/storage/QcEntityRecord.h"

#include <QHash>
#include <QSet>
#include <QStringList>

class QcEntities;

// Turns the entities of a running world into the records saved per chunk and back (decisions E-366,
// E-374).
namespace QcEntityRecords {

// A record for every chunk with entities to keep (persistent creatures, items and waiting drops,
// falling blocks, stands), each entry with its place in its kind's table (items first, then the
// drops waiting to appear). `populated` is left to the caller.
QHash<QcChunkPos, QcEntityRecord> collect(const QcEntities &entities);
// The same for the chunks in `chunks`, and the entities are taken out of the world: the records of
// chunks that leave it. Creatures that are not persistent and sliding blocks go without a record.
QHash<QcChunkPos, QcEntityRecord> take(QcEntities &entities, const QSet<QcChunkPos> &chunks);
// Puts the entities of a chunk into the world, each kind in its saved order after those already
// there, with new ids given to items, falling blocks, creatures and stands in that order. The limits
// of the kinds apply as when they are made (items beyond the total limit wait, falling blocks and
// stands beyond theirs are left out); returns the warnings about what was left out.
QStringList apply(const QcEntityRecord &record, QcEntities &entities);

} // namespace QcEntityRecords
