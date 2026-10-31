// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/core/QcBlockPos.h"
#include "common/protocol/QcWorldMessages.h"

#include <QList>

#include <deque>

class QcBlastTable;
class QcServerBlocks;
class QcServerFire;
class QcServerState;

/*! \brief The block side of explosions (decision E-602): the cells a blast breaks wait in order and
    are worked through at most \c {explosionBlocksPerTick} per tick (QcServerLimits), the rest in
    the next ticks; nothing is ever lost on the way, but a cell whose chunk left the world in the
    meantime is skipped.

    A cell holding an explosive is not broken but handed back to be lit (a chain reaction); any
    other cell becomes air quietly, drops its items with the blast's chance, lets the plants and
    an infested block react, and with a blast that sets fires may catch fire. Doors and beds
    break as a whole through QcServerBlocks. The clients get every cell set in a tick as one
    QcMsg::BlockBatch (several when there are more than QcMsg::BlockBatch::maxUpdates), so they
    relight and mesh each section once. */
class QcBlastBlocks
{
public:
    /*! \brief What a blast does to the cells it breaks: drops each with \c {dropChance} (the
        reference game's 1 / power) and, with \c {fire}, lights one in three that stands on a
        solid block. */
    struct Effects
    {
        /*! The cell of the blast's centre (its dice). */
        QcBlockPos centre;
        /*! The chance that a broken cell drops its items. */
        double dropChance = 0.25;
        /*! The blast lights fires. */
        bool fire = false;
    };

    /*! \brief An explosive a blast reached, to be lit with a short random chain fuse
        (QcServerExplosions). */
    struct Chained
    {
        /*! The position of the explosive. */
        QcBlockPos pos;
        /*! The explosive block. */
        QcBlockId block = qcAirBlockId;
    };

    QcBlastBlocks(QcServerState &state, QcServerBlocks &blocks, QcServerFire &fire, const QcBlastTable &table);

    /*! Adds the \a cells a blast with \a effects breaks to the waiting cells. */
    void add(const QList<QcBlockPos> &cells, const Effects &effects);
    /*! Works through at most \a budget waiting cells; returns the explosives among them, still
        in place. */
    QList<Chained> take(qint64 budget);
    /*! Sets \a pos to air without telling the clients yet (a lit explosive leaves its cell).
        Returns \c false when the block could not be set. */
    bool clear(QcBlockPos pos);
    /*! Sends the cells set since the last flush to the clients that have their chunks, then
        lights the fires of blasts with fire. */
    void flush();

    /*! Forgets the waiting cells (the game rule explosionBlockDamage was switched off). */
    void drop() { m_waiting.clear(); }
    /*! Returns the number of waiting cells. */
    qsizetype waiting() const { return static_cast<qsizetype>(m_waiting.size()); }
    /*! Returns the blocks broken since the server started. */
    quint64 broken() const { return m_broken; }

    /*! Returns \a updates cut into batches of at most \a most updates each, in order. */
    static QList<QcMsg::BlockBatch> pieces(const QList<QcMsg::BlockUpdate> &updates, qsizetype most);

private:
    struct Work
    {
        QcBlockPos pos;
        Effects effects;
    };

    void breakCell(const Work &work, QcBlockId block);
    void sendBatch();
    // The dice of a blast's decisions on one cell, from the seed, the blast's centre and the cell.
    quint32 roll(const Work &work, quint32 salt, quint32 bound) const;

    QcServerState &m_state;
    QcServerBlocks &m_blocks;
    QcServerFire &m_fire;
    const QcBlastTable &m_table;
    std::deque<Work> m_waiting;
    QList<QcMsg::BlockUpdate> m_batch;
    QList<QcBlockPos> m_fires;
    quint64 m_broken = 0;
};
