// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/core/QcServerLimits.h"
#include "server/entities/QcFallingBlock.h"
#include "server/entities/QcPrimedExplosive.h"

#include <QList>

#include <functional>

class QcBlockAccess;

/*! \brief The lit explosives of a world (decision E-601), the table of their kind in the entity
    register: each falls like a falling block (the shared physics, the same size) and counts its
    fuse down while its chunk is loaded.

    Their number is bounded (QcServerLimits::primedExplosives): beyond it nothing is lit, and an
    explosive stays a block. */
class QcPrimedExplosives
{
public:
    /*! Edge length of the cube, like a falling block's. */
    static constexpr double size = 0.98;
    /*! The hop a lit explosive makes, metres per second up and to its side. \qcshared */
    static constexpr double hopUp = 4.0;
    static constexpr double hopSide = 0.4;

    /*! Returns whether no more explosives may be lit. */
    bool isFull() const { return m_primed.size() >= m_limit; }
    /*! Sets the number of explosives lit at once to at most \a limit. */
    void setLimit(qsizetype limit) { m_limit = limit; }
    /*! Lights the explosive \a block standing at \a cell with \a fuse ticks, with id \a id and
        its hop to the side \a sideways (radians). Returns \c false when the table is full. */
    bool prime(QcBlockPos cell, QcBlockId block, int fuse, quint32 id, double sideways);
    /*! Runs one tick of \a dt seconds in \a world for the awake ones (QcEntities::settle): physics
        and their fuses. Returns the ones whose fuse ran out, at most \a most, in the order of the
        table, and takes them out; the others with a fuse run out wait at 0 for the next tick. Those
        that fall below the world are gone. */
    QList<QcPrimedExplosive> tick(const QcBlockAccess &world, double dt, qsizetype most);

    /*! Returns every lit explosive. */
    const QList<QcPrimedExplosive> &all() const { return m_primed; }
    /*! Returns the lit explosives for moving them from outside (pistons push them); never add or
        remove through it. */
    QList<QcPrimedExplosive> &editableAll() { return m_primed; }
    /*! Removes those inside \a chunk. Returns how many. */
    qsizetype removeInChunk(QcChunkPos chunk);
    /*! Takes the saved \a primed back (a chunk came back, decision E-374), ids from
        \a nextEntityId; returns the ones left out at the limit. */
    QList<QcPrimedExplosive> adopt(const QList<QcPrimedExplosive> &primed, const std::function<quint32()> &nextEntityId);
    /*! Returns \a primed put out: a falling block of its explosive where it is, which lands as the
        block (a saved one beyond the limit is not lost). */
    static QcFallingBlock unlit(const QcPrimedExplosive &primed);

private:
    QList<QcPrimedExplosive> m_primed;
    qsizetype m_limit = static_cast<qsizetype>(QcServerLimits{}.primedExplosives);
};
