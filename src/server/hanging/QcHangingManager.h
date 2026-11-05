// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "server/core/QcServerLimits.h"
#include "server/entities/QcHangingEntity.h"

#include <QHash>
#include <QList>
#include <QSet>

#include <functional>
#include <optional>

/*! \brief Item frames and paintings of a world (decision E-790): placed entities without AI, health
    or physics.

    Pure data; QcServerHanging does what players and the world do to them. */
class QcHangingManager
{
public:
    /*! Frames and paintings in the world by default (QcServerLimits::maxHangings). */
    static constexpr int maxHangings = static_cast<int>(QcServerLimits{}.maxHangings);
    /*! Every frame and painting checks its wall and room again within this many ticks, also
        without a block change next to it (decision E-790; changes outside the server's block
        changes). */
    static constexpr int supportCheckTicks = 100;

    /*! Returns every frame and painting. */
    const QList<QcHangingEntity> &all() const { return m_all; }
    /*! Returns the frames and paintings for waking and settling from outside (QcEntities); never
        add, remove or move through it (the cell index would go stale). */
    QList<QcHangingEntity> &editableAll() { return m_all; }
    /*! Returns the frame or painting with entity \a id, or null when there is none. */
    QcHangingEntity *find(quint32 id);
    /*! Returns the frame or painting with entity \a id, or null when there is none. */
    const QcHangingEntity *find(quint32 id) const;
    /*! Returns the frame or painting that covers \a cell on \a face; null for none. */
    const QcHangingEntity *at(QcBlockPos cell, QcBlockFace face) const;
    /*! Adds \a hanging. Returns \c false when the limit is reached. */
    bool add(const QcHangingEntity &hanging);
    /*! Removes the frame or painting with entity \a id. Returns it (to drop what it carried), or
        nothing when there is none. */
    std::optional<QcHangingEntity> remove(quint32 id);
    /*! Returns the number of frames and paintings the world may hold. */
    qsizetype limit() const { return m_limit; }
    /*! Sets the number of frames and paintings the world may hold to \a limit. */
    void setLimit(qsizetype limit) { m_limit = limit; }
    /*! Adds the \a hangings of a chunk that came back (decision E-374); returns how many were
        left out at the limit. */
    qsizetype adopt(const QList<QcHangingEntity> &hangings);
    /*! Removes those for which \a matches is true (their chunk leaves the world). */
    void removeIf(const std::function<bool(const QcEntity &)> &matches);

    /*! A block changed at \a pos: the frames and paintings it holds or stands in check again
        (decision E-790). One hash lookup, so a change far from any costs nothing. */
    void noteBlockChange(QcBlockPos pos);
    /*! Returns the ids to check this tick: those next to changed blocks and the next slice of
        the round in which every one is checked once per supportCheckTicks ticks. */
    QList<quint32> takeDueChecks();

private:
    // The cells and supporting blocks of `hanging`, the keys of the cell index.
    static QList<QcBlockPos> touchedBlocks(const QcHangingEntity &hanging);
    void index(const QcHangingEntity &hanging);
    void unindex(const QcHangingEntity &hanging);
    // Rebuilds where each id stands in m_all after entries left it.
    void renumber();

    QList<QcHangingEntity> m_all;
    qsizetype m_limit = maxHangings;
    // Where each id stands in m_all.
    QHash<quint32, qsizetype> m_position;
    // Ids by the cells they cover and the blocks that hold them.
    QHash<QcBlockPos, QList<quint32>> m_byBlock;
    QSet<quint32> m_changed;
    // Where the round of checks goes on in m_all.
    qsizetype m_roundNext = 0;
};
