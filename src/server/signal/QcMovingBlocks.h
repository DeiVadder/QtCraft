// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/blocks/QcBlockDefinition.h"
#include "common/core/QcBlockPos.h"
#include "common/core/QcVec3.h"
#include "server/core/QcServerLimits.h"
#include "server/entities/QcMovingBlock.h"

#include <QList>

/*! \brief Blocks a piston moves, as the clients see them (decision E-337): the world holds every
    moved block at its new cell at once (saves, chunk borders and other players see one consistent
    world), while the clients get the new cells only after the move and meanwhile see the block as
    an entity that slides there.

    Only the look of the move lasts; nothing waits for it. */
class QcMovingBlocks
{
public:
    /*! Ticks a move lasts for the clients. */
    static constexpr int moveTicks = 2;
    /*! Moves shown at once at most; beyond, blocks jump without the slide.
        Blocks shown sliding at once by default (QcServerLimits::movingBlocksShown). */
    static constexpr qsizetype maxMoving = QcServerLimits{}.movingBlocksShown;
    /*! Sets the moves shown at once at most to \a limit. */
    void setMaxMoving(qsizetype limit) { m_maxMoving = limit; }

    /*! Shows \a moving sliding. Returns \c false (and nothing shown) when too many move already.
        The entity's position follows the slide (positionAt), updated by tick(). */
    bool add(const QcMovingBlock &moving);
    /*! Returns the cells whose move ends at \a now (the clients get their block now); moves are
        forgotten one tick later, after the clients had the block, so nothing flickers. */
    QList<QcBlockPos> tick(quint64 now);
    /*! Returns where the sliding block of \a moving is at \a now: its feet, as for falling
        blocks. */
    static QcVec3 positionAt(const QcMovingBlock &moving, quint64 now);
    /*! Returns whether the clients still wait for the block of \a cell. */
    bool isMovingTo(QcBlockPos cell) const;
    /*! Forgets the slides into \a cell (a piston head that retracts before it arrived): the
        clients stop showing them and get no block from them. */
    void cancelTo(QcBlockPos cell);
    /*! Returns every move shown. */
    const QList<QcMovingBlock> &all() const { return m_moving; }
    /*! Returns the moves for the entity register (QcEntities); never add or remove through it. */
    QList<QcMovingBlock> &editableAll() { return m_moving; }

private:
    QList<QcMovingBlock> m_moving;
    qsizetype m_maxMoving = maxMoving;
};
