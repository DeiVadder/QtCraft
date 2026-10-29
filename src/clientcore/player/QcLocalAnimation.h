// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/meshing/QcEntityPose.h"
#include "clientcore/meshing/QcHandSwing.h"
#include "clientcore/player/QcHandTimers.h"
#include "clientcore/session/QcClientEntities.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcSwingSpeed.h"
#include "common/physics/QcEntityMotion.h"

class QcPrediction;

// Animation of the local player between server updates: the timers of the arm swing, using,
// switching the held item and landing (in ticks, negative while not running) and the distance
// walked for the stride. The poses for the renderer come from these timers and the predicted
// motion; poseOf() builds the pose of a mirrored entity the same way.
class QcLocalAnimation
{
public:
    // Animation lengths in ticks; those of the hand are QcHandTimers'. The own swing lasts as long
    // as the QcSwingSpeed given to tick() says, swingTicks without effects.
    static constexpr int swingTicks = QcClientEntities::swingTicks;
    static constexpr int deathTicks = QcClientEntities::deathTicks;

    // Advances the timers by one tick. Holding another item (qcNoItem: empty hand) starts the
    // switch; `mining` keeps the arm swinging, one swing after the other without a gap. `speed`
    // sets the length of the swings that start from now on (decision E-881); a running one keeps
    // its length.
    void tick(QcItemId held, bool mining, QcSwingSpeed speed);
    // After a predicted step on foot: the distance walked and a landing.
    void stepped(const QcEntityMotion &previous, const QcEntityMotion &current);

    // Starts the arm swing (a click's short one) or the use animation.
    void swing() { startSwing(QcHandSwing::Style::Click); }
    void use() { m_useTicks = 0; }

    // Pose of the local player from the prediction; `alpha` interpolates within the current tick,
    // `drawProgress` is the drawn launcher's (QcClientLauncher::progress).
    QcEntityPose localPose(double alpha, const QcPrediction &prediction, double drawProgress) const;
    QcHandTimers handTimers(double alpha) const;
    // Pose of a mirrored entity from its server state and client timers.
    static QcEntityPose poseOf(const QcClientEntity &entity, double alpha);

private:
    // Starts a swing of `style`; it keeps that style to its end (decision E-881), so a tick
    // without mining (the block just broke) does not switch a running mining swing to a click's.
    void startSwing(QcHandSwing::Style style)
    {
        m_swingTicks = 0;
        m_swingLength = m_nextSwingLength;
        m_swingStyle = style;
    }

    int m_swingTicks = -1;
    // Length of the running swing and of the next one, in ticks.
    int m_swingLength = swingTicks;
    int m_nextSwingLength = swingTicks;
    QcHandSwing::Style m_swingStyle = QcHandSwing::Style::Click;
    int m_useTicks = -1;
    int m_switchTicks = -1;
    int m_landingTicks = -1;
    double m_walkDistance = 0.0;
    double m_lastStep = 0.0;
    QcItemId m_lastHeld = qcNoItem;
};
