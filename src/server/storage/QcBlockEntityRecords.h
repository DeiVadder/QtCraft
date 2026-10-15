#pragma once

#include "QcBlockEntityRecord.h"

#include <QList>
#include <QStringList>

class QcBlockEntities;
struct QcCircuitState;
struct QcHopperQueue;

// Turns the block entities of a running world into the records saved per chunk and back (decision
// E-358): ticks become ticks left (or since) at `now` and absolute ticks again when read.
namespace QcBlockEntityRecords {

// Every chunk with something to keep, sorted by x, then z.
QList<QcBlockEntityRecord> collect(const QcBlockEntities &entities, const QcCircuitState &circuits, quint64 now);
// Puts the records into the world: containers, machines and saplings with their contents and
// timers, memories, scheduled ticks; hoppers resting wake when their cooldown ends, positions the
// circuits were going to look at are changes again (waiting for any chunk they read that is not
// loaded). The
// limits do not refuse entries of a save; returns what had to be left out (taken positions).
QStringList apply(const QList<QcBlockEntityRecord> &records, QcBlockEntities &entities, QcCircuitState &circuits, QcHopperQueue &hoppers,
           quint64 now);
// The record of a chunk that leaves the world (decision E-374), with everything of it taken out of
// the block entities, the circuits' work and the hoppers' queue. `blockUpdates` is left to the caller.
QcBlockEntityRecord take(QcChunkPos chunk, QcBlockEntities &entities, QcCircuitState &circuits, QcHopperQueue &hoppers, quint64 now);

} // namespace QcBlockEntityRecords
