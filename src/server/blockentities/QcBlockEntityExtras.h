#pragma once

#include "common/core/QcBlockPos.h"
#include "common/core/QcChunkPos.h"
#include "server/storage/QcBlockEntityRecord.h"

#include <QHash>
#include <QSet>

class QcBlockEntities;
class QcFireQueue;
class QcSpawners;

// What the loaded chunks keep about their block entities besides the entities themselves (part of
// QcServerWorld, decision E-716); saved with the chunk and forgotten with it.
//
// Furnished chunks had their generated block entities (the chests' loot, the spawners' kinds;
// decision E-520) put in once (QcFurnishing). Kept parts are what the saved block entities hold that
// this version does not read (decision E-440): unknown tables and tails of newer versions, written
// back when the chunk is saved or leaves the world (QcChunkLoading). The tail of an entry goes when a
// block set in the world ends the entry, so a block built anew at its place starts without it.
class QcBlockEntityExtras
{
public:
    bool isFurnished(QcChunkPos pos) const { return m_furnished.contains(pos); }
    void noteFurnished(QcChunkPos pos) { m_furnished.insert(pos); }

    const QHash<QcChunkPos, QcBlockEntityKept> &keptParts() const { return m_parts; }
    // Holds `parts` for a loaded chunk (nothing when they are empty).
    void keepParts(QcChunkPos pos, QcBlockEntityKept parts);
    [[nodiscard]] QcBlockEntityKept takeParts(QcChunkPos pos) { return m_parts.take(pos); }
    // Whether any chunk keeps parts (a block set needs no check otherwise).
    bool hasKeptParts() const { return !m_parts.isEmpty(); }
    // Drops the kept tails of the entries at `pos` that the block just set there ended: those whose
    // container, machine, sapling, memory, fire or spawner is gone.
    void dropOrphanTails(QcBlockPos pos, const QcBlockEntities &entities, const QcFireQueue &fires, const QcSpawners &spawners);

    // Forgets a chunk that left the world or is generated again.
    void forgetChunk(QcChunkPos pos);

private:
    QSet<QcChunkPos> m_furnished;
    QHash<QcChunkPos, QcBlockEntityKept> m_parts;
};
