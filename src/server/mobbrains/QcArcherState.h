// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/items/QcItemDefinition.h"
#include "common/core/QcVec3.h"

#include <QtGlobal>

/*! \brief What an archer (QcMobDefinition::Ai::Archer, decision E-426) does between ticks: its
    draw, the side it circles to, where it last saw its target (for the lead of its shots) and its
    drink.

    A component of the creature (QcCreature::archer); not saved, like all hostile creatures. */
struct QcArcherState
{
    /*! The tick of its brain (QcMobBrain::Context::tick) it began to draw at; -1 while it does
        not draw. */
    qint64 drawStart = -1;
    /*! Circles to the left (-1) or the right (1) of its target until strafeTicks run out. */
    int strafeSide = 1;
    /*! Ticks it keeps circling to strafeSide. */
    int strafeTicks = 0;
    /*! The target it last saw (entity id). */
    quint32 target = 0;
    /*! The tick it last saw the target. */
    quint64 seenTick = 0;
    /*! Where it last saw the target. */
    QcVec3 targetPosition;
    /*! The target's velocity (smoothed from the change of position). */
    QcVec3 targetVelocity;

    /*! A drink (QcMobDefinition::drinks, decisions E-623, E-1105; QcMobDrinking): the game tick
        it began at (-1 while it does not drink). */
    qint64 drinkStart = -1;
    /*! The potion it drinks (an item that is drunk); qcNoItem while it does not drink. */
    QcItemId drinkItem = qcNoItem;

    /*! Returns whether it draws its bow. */
    bool drawing() const { return drawStart >= 0; }
    /*! Returns whether it drinks. */
    bool drinking() const { return drinkStart >= 0; }

    /*! Keeps that the target \a targetId is at \a position at \a tick and its smoothed velocity
        (QcArcherBrain.cpp); \a dt is the length of a tick. */
    void track(quint32 targetId, const QcVec3 &position, quint64 tick, double dt);
};
