#pragma once

#include "server/storage/QcBlockEntityRecord.h"

#include <QHash>
#include <QList>
#include <QStringList>

class QcBlockEntities;
struct QcCircuitState;
struct QcHopperQueue;

// Turns the block entities of a running world into the records saved per chunk and back (decision
// E-358): ticks become ticks left (or since) at `now` and absolute ticks again when read.
namespace QcBlockEntityRecords {

// Every chunk with something to keep, sorted by x, then z. An entry with a kept tail of a newer
// version (`kept`, QcServerWorld::blockEntityParts) is saved even when it holds nothing else.
QList<QcBlockEntityRecord> collect(const QcBlockEntities &entities, const QcCircuitState &circuits, quint64 now,
                                   const QHash<QcChunkPos, QcBlockEntityKept> &kept = {});
// Puts the records into the world: containers, machines and saplings with their contents and
// timers, memories, scheduled ticks; hoppers resting wake when their cooldown ends, positions the
// circuits were going to look at are changes again (waiting for any chunk they read that is not
// loaded). The
// limits do not refuse entries of a save; returns what had to be left out (taken positions). The
// records' kept parts are left to the caller (QcServerWorld::keepBlockEntityParts).
QStringList apply(const QList<QcBlockEntityRecord> &records, QcBlockEntities &entities, QcCircuitState &circuits, QcHopperQueue &hoppers,
           quint64 now);
// Fills the record of a chunk that leaves the world (decision E-374), `record.chunk`, with
// everything of it taken out of the block entities, the circuits' work and the hoppers' queue; its
// `kept` parts, set by the caller, decide like in collect(). `blockUpdates` and `fires` are left to
// the caller.
void take(QcBlockEntityRecord &record, QcBlockEntities &entities, QcCircuitState &circuits, QcHopperQueue &hoppers, quint64 now);
// Drops the kept tails of entries the record no longer holds (a container broken since it was read),
// once the record is complete (fires included): a tail goes back only with its entry.
void pruneKept(QcBlockEntityRecord &record);

} // namespace QcBlockEntityRecords
