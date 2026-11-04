// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/camera/QcHurtTilt.h"
#include "clientcore/figures/QcEntityPose.h"
#include "clientcore/figures/QcHandSwing.h"
#include "clientcore/player/QcHandTimers.h"
#include "clientcore/session/QcClientEntities.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/gameplay/QcSwingSpeed.h"
#include "common/physics/QcEntityMotion.h"

#include <algorithm>

class QcPrediction;

/*! \brief Animation of the local player between server updates: the timers of the arm swing, using,
    switching the held item and landing (in ticks, negative while not running) and the distance
    walked for the stride.

    The poses for the renderer come from these timers and the predicted motion; poseOf() builds
    the pose of a mirrored entity the same way. */
class QcLocalAnimation
{
public:
    /*! Animation lengths in ticks; those of the hand are QcHandTimers'. The own swing lasts as
        long as the QcSwingSpeed given to tick() says, swingTicks without effects. \qcshared */
    static constexpr int swingTicks = QcClientEntities::swingTicks;
    static constexpr int deathTicks = QcClientEntities::deathTicks;
    /*! The hurt time of the own player in ticks: the red flash of the figure and the tilt of the
        view, as long as the flash of others (decision E-971). */
    static constexpr int hurtTicks = QcClientEntities::hurtFlashTicks;

    /*! Advances the timers by one tick. Holding another item \a held (qcNoItem: empty hand)
        starts the switch; \a mining keeps the arm swinging, one swing after the other without a
        gap. \a speed sets the length of the swings that start from now on (decision E-881); a
        running one keeps its length. \a draw is the power of the drawn launcher (negative when
        none): the hand comes to it or leaves it (QcHandTimers::aim, decision E-984); another
        item ends that at once. */
    void tick(QcItemId held, bool mining, QcSwingSpeed speed, double draw);
    /*! After a predicted step on foot from \a previous to \a current: the distance walked and a
        landing. */
    void stepped(const QcEntityMotion &previous, const QcEntityMotion &current);

    /*! Starts the arm swing (a click's short one) or the use animation. */
    void swing() { startSwing(QcHandSwing::Style::Click); }
    /*! Starts the use animation. */
    void use() { m_useTicks = 0; }
    /*! A fresh hit hurt the player from the world yaw \a from (none: without a direction) while
        they looked along \a viewYaw: starts the hurt time (decision E-971). */
    void hurt(std::optional<float> from, float viewYaw);
    /*! Returns the tilt of the view \a alpha into the current tick (none outside the hurt time). */
    [[nodiscard]] QcHurtTilt::Tilt hurtTilt(double alpha) const;

    /*! Returns the pose of the local player from \a prediction; \a alpha interpolates within the
        current tick, \a drawProgress is the drawn launcher's (QcClientLauncher::progress). */
    QcEntityPose localPose(double alpha, const QcPrediction &prediction, double drawProgress) const;
    /*! Returns the progress of the hand animations \a alpha into the current tick. */
    QcHandTimers handTimers(double alpha) const;
    /*! Returns the pose of the mirrored \a entity from its server state and client timers,
        \a alpha into the current tick. */
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

    int aimTicks() const { return m_aimTicks; }
    void setAimTicks(int ticks) { m_aimTicks = std::clamp(ticks, 0, QcHandTimers::aimTicks); }
    bool drawing() const { return m_drawing; }
    void setDrawing(bool drawing) { m_drawing = drawing; }
    double runningDraw() const { return m_runningDraw; }
    void setRunningDraw(double draw) { m_runningDraw = draw; }
    double lastDraw() const { return m_lastDraw; }
    void setLastDraw(double draw) { m_lastDraw = draw; }

    int m_swingTicks = -1;
    // Length of the running swing and of the next one, in ticks.
    int m_swingLength = swingTicks;
    int m_nextSwingLength = swingTicks;
    QcHandSwing::Style m_swingStyle = QcHandSwing::Style::Click;
    int m_useTicks = -1;
    int m_switchTicks = -1;
    int m_landingTicks = -1;
    // How many ticks the hand has come towards a drawn launcher (0..QcHandTimers::aimTicks) and
    // whether it is drawn, so the aim rises or falls within the tick; the power of the running
    // draw and the one the hand shows at least (QcHandTimers::lastDraw), which falls by
    // lastDrawFade per tick while it lies above a new, weaker draw.
    static constexpr double lastDrawFade = 1.0 / QcHandTimers::aimTicks;
    int m_aimTicks = 0;
    bool m_drawing = false;
    double m_runningDraw = 0.0;
    double m_lastDraw = 0.0;
    int m_hurtTicks = -1; // NOLINT(qc-member-access): a tick timer like the others here, advanced by reference in tick()
    // Where the running hurt came from, in degrees relative to the view at the hit.
    std::optional<double> m_hurtFrom; // NOLINT(qc-member-access): set with m_hurtTicks in hurt(), read in hurtTilt()
    double m_walkDistance = 0.0;
    double m_lastStep = 0.0;
    QcItemId m_lastHeld = qcNoItem;
};
