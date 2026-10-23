#pragma once

#include "server/core/QcServerLimits.h"
#include "server/entities/QcFallingBlock.h"
#include "server/entities/QcPrimedExplosive.h"

#include <QList>

#include <functional>

class QcBlockAccess;

// The lit explosives of a world (decision E-601), the table of their kind in the entity register:
// each falls like a falling block (the shared physics, the same size) and counts its fuse down while
// its chunk is loaded. Their number is bounded (QcServerLimits::primedExplosives): beyond it nothing
// is lit, and an explosive stays a block.
class QcPrimedExplosives
{
public:
    // Edge length of the cube, like a falling block's.
    static constexpr double size = 0.98;
    // The hop a lit explosive makes, metres per second up and to its side.
    static constexpr double hopUp = 4.0;
    static constexpr double hopSide = 0.4;

    bool isFull() const { return m_primed.size() >= m_limit; }
    void setLimit(qsizetype limit) { m_limit = limit; }
    // Lights the explosive `block` standing at `cell` with `fuse` ticks, with id `id` and its hop to
    // the side `sideways` (radians); false when the table is full.
    bool prime(QcBlockPos cell, QcBlockId block, int fuse, quint32 id, double sideways);
    // One tick of the awake ones (QcEntities::settle): physics and their fuses. Returns the ones whose
    // fuse ran out, at most `most`, in the order of the table, and takes them out; the others with
    // a fuse run out wait at 0 for the next tick. Those that fall below the world are gone.
    QList<QcPrimedExplosive> tick(const QcBlockAccess &world, double dt, qsizetype most);

    const QList<QcPrimedExplosive> &all() const { return m_primed; }
    // For moving them from outside (pistons push them); never add or remove through it.
    QList<QcPrimedExplosive> &editableAll() { return m_primed; }
    // Removes those inside the chunk; returns how many.
    qsizetype removeInChunk(QcChunkPos chunk);
    // Takes saved ones back (a chunk came back, decision E-374), ids from `nextEntityId`; returns
    // the ones left out at the limit.
    QList<QcPrimedExplosive> adopt(const QList<QcPrimedExplosive> &primed, const std::function<quint32()> &nextEntityId);
    // A lit explosive put out: a falling block of its explosive where it is, which lands as the
    // block (a saved one beyond the limit is not lost).
    static QcFallingBlock unlit(const QcPrimedExplosive &primed);

private:
    QList<QcPrimedExplosive> m_primed;
    qsizetype m_limit = static_cast<qsizetype>(QcServerLimits{}.primedExplosives);
};
