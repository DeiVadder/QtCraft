// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/physics/QcPlayerInput.h"

#include <QList>
#include <QtGlobal>

/*! \brief What status effects did to a player's moving lately, by a count that the client quotes
    in its inputs (decision E-992).

    The client hears of a change of the effects a round trip after the server made it, so the
    inputs in flight were predicted with the old ones. Stepping them with the new ones would
    correct every change of speed, jump or fall: instead every change gets the next count, sent
    with the health, and the server steps each input with the effects of the count it quotes. An
    effect thus starts and ends on the same input on both sides.

    The client's word is bounded: a count stays good for graceTicks after the next one began, and
    an unknown count means the current effects. */
class QcMovementEpochs
{
public:
    /*! How long an old count stays good after the next one began: far more than a round trip. */
    static constexpr quint64 graceTicks = 100;
    /*! How many old counts are kept. */
    static constexpr qsizetype maxKept = 8;

    /*! Takes over \a effects in \a tick: a change begins the next count. */
    void update(QcMovementEffects effects, quint64 tick);
    /*! Returns the current count. */
    [[nodiscard]] quint16 epoch() const { return m_epoch; }
    /*! Returns the current effects. */
    [[nodiscard]] const QcMovementEffects &current() const { return m_current; }
    /*! Returns the effects to step an input with that quotes \a epoch in \a tick. */
    [[nodiscard]] QcMovementEffects forInput(quint16 epoch, quint64 tick) const;
    /*! Drops the old counts, so that every input moves with the current effects (after a death,
        a respawn or a teleport). */
    void forgetPast() { m_past.clear(); }

private:
    // Sets the current effects.
    void setCurrent(QcMovementEffects effects) { m_current = effects; }

    // An old count, its effects and the tick it ended in.
    struct Past
    {
        quint16 epoch = 0;
        QcMovementEffects effects;
        quint64 endTick = 0;
    };

    QList<Past> m_past; // NOLINT(qc-member-access): history kept by update(), read by forInput() only
    quint16 m_epoch = 0;
    QcMovementEffects m_current;
};
