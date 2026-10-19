#pragma once

#include "common/core/QcVec3.h"

#include <QtGlobal>

// What an archer (QcMobDefinition::Ai::Archer, decision E-426) does between ticks: its draw, the
// side it circles to, where it last saw its target (for the lead of its shots) and its drink. A component of
// the creature (QcCreature::archer); not saved, like all hostile creatures.
struct QcArcherState
{
    // The tick of its brain (QcMobBrain::Context::tick) it began to draw at; -1 while it does not draw.
    qint64 drawStart = -1;
    // Circles to the left (-1) or the right (1) of its target until strafeTicks run out.
    int strafeSide = 1;
    int strafeTicks = 0;
    // The target it last saw, where and with which velocity (smoothed from the change of position).
    quint32 target = 0;
    quint64 seenTick = 0;
    QcVec3 targetPosition;
    QcVec3 targetVelocity;

    // A drink to heal (QcMobDefinition::drinks, decision E-623; QcMobDrinking): the game tick it
    // began at (-1 while it does not drink) and the first tick it may drink again.
    qint64 drinkStart = -1;
    quint64 drinkReady = 0;

    bool drawing() const { return drawStart >= 0; }
    bool drinking() const { return drinkStart >= 0; }

    // Keeps where the target `targetId` is at `tick` and its smoothed velocity (QcArcherBrain.cpp);
    // `dt` is the length of a tick.
    void track(quint32 targetId, const QcVec3 &position, quint64 tick, double dt);
};
