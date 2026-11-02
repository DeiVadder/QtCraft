// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/figures/QcEntityPose.h"
#include "common/core/QcVec3.h"

/*! \brief How a figure moves, as far as its cape cares: speeds in m/s in the figure's own frame. */
struct QcCapeDrive
{
    /*! Along the facing direction (negative when walking backwards). */
    double forwardSpeed = 0.0;
    /*! To the figure's right. */
    double sideSpeed = 0.0;
    /*! Upwards (negative when falling). */
    double verticalSpeed = 0.0;
    /*! Standing or walking on the ground. */
    bool onGround = true;
    /*! In water. */
    bool inWater = false;
    /*! Sneaking. */
    bool sneaking = false;

    /*! Returns the drive of a figure moving with \a velocity in world space, facing \a yaw
        (degrees, 0 faces -Z, 90 faces +X), in \a pose. */
    static QcCapeDrive from(const QcVec3 &velocity, double yaw, const QcEntityPose &pose);
};

/*! \brief The swing of a cape (decision E-240): a damped spring per axis pulls the cape towards an
    angle that follows from the movement: lifted backwards by walking and more by sprinting, upwards
    when falling, pressed to the back when rising or sneaking, held off the swinging legs when
    walking backwards, swaying gently when standing.

    Integrated in fixed steps, so the result does not depend on the frame rate; GUI-free. */
class QcCapeSwing
{
public:
    /*! \brief Degrees against the back as in QcAnimator::BodyPose::capeLift/capeSway. */
    struct Angles
    {
        /*! Lifted backwards, 0 hangs along the back. */
        double lift = 0.0;
        /*! Swung to the figure's left. */
        double sway = 0.0;
    };

    /*! Lowest lift. */
    static constexpr double minLift = 0.0;
    /*! Highest lift. */
    static constexpr double maxLift = 150.0;
    /*! Widest sway to either side. */
    static constexpr double maxSway = 25.0;
    /*! Lift at rest: the cape stands off the back a little. */
    static constexpr double restLift = 5.0;
    /*! Integration step and the longest time one call integrates (a longer pause is cut short).
        \qcshared */
    static constexpr double step = 1.0 / 120.0;
    static constexpr double maxAdvance = 0.25;

    /*! Returns where the spring pulls the cape for \a drive at \a time (seconds of a
        free-running clock, driving the idle sway and the flutter). */
    static Angles target(const QcCapeDrive &drive, double time);

    /*! Moves the cape \a seconds further, driven by \a drive; \a time is the clock at the end of
        that span. */
    void advance(const QcCapeDrive &drive, double seconds, double time);
    /*! Puts the cape at rest on its target for \a drive at \a time (first sight, fixed
        screenshots). */
    void settle(const QcCapeDrive &drive, double time);
    /*! Returns the cape's current angles. */
    Angles angles() const { return {m_lift.angle, m_sway.angle}; }

private:
    struct Axis
    {
        double angle = 0.0;
        double velocity = 0.0;

        void step(double target, double low, double high);
    };

    Axis m_lift{restLift, 0.0};
    Axis m_sway;
    // Time not integrated yet (less than one step).
    double m_pending = 0.0;
};
