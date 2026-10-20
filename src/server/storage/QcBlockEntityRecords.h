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
// version (`kept`, QcBlockEntityExtras::keptParts) is saved even when it holds nothing else.
QList<QcBlockEntityRecord> collect(const QcBlockEntities &entities, const QcCircuitState &circuits, quint64 now,
                                   const QHash<QcChunkPos, QcBlockEntityKept> &kept = {});
// Puts the records into the world: containers, machines and saplings with their contents and
// timers, memories, scheduled ticks; hoppers resting wake when their cooldown ends, positions the
// circuits were going to look at are changes again (waiting for any chunk they read that is not
// loaded). The
// limits do not refuse entries of a save; returns what had to be left out (taken positions). The
// records' kept parts are left to the caller (QcBlockEntityExtras::keepParts).
QStringList apply(const QList<QcBlockEntityRecord> &records, QcBlockEntities &entities, QcCircuitState &circuits, QcHopperQueue &hoppers,
           quint64 now);
// A chunk leaving the world fills its record with QcBlockEntityRecord::takeFrom, and drops the kept
// tails of entries gone with QcBlockEntityRecord::pruneKept.

} // namespace QcBlockEntityRecords
