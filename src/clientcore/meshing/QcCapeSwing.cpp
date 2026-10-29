// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/meshing/QcCapeSwing.h"

#include "common/physics/QcPhysics.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace {

// Degrees of lift per m/s: walking forwards lifts the cape, sprinting (above walking speed) more,
// walking backwards presses it to the back; falling lifts it up, rising presses it down. In water
// it drags along the back.
constexpr double liftPerSpeed = 8.0;
constexpr double sprintLiftPerSpeed = 10.0;
constexpr double pressPerSpeed = 4.0;
constexpr double fallLiftPerSpeed = 7.0;
constexpr double risePressPerSpeed = 4.0;
constexpr double waterLiftPerSpeed = 3.0;
// Walking backwards the legs swing back into the cape (up to 38 degrees at walking speed): on the
// ground it stands off the back at least this far at walking speed, less when slower.
constexpr double backstepLift = 24.0;
// Degrees of sway per m/s sideways (the cape trails behind a sidestep).
constexpr double swayPerSpeed = 5.0;
// From this speed on the cape flutters fully instead of swaying at rest.
constexpr double flutterSpeed = 2.0;

// Spring towards the target: about 1.1 swings per second, damped so that it overshoots a little
// and settles within about two seconds.
constexpr double frequency = 1.1;
constexpr double dampingRatio = 0.45;
constexpr double stiffness = (2.0 * M_PI * frequency) * (2.0 * M_PI * frequency);
constexpr double damping = 2.0 * dampingRatio * 2.0 * M_PI * frequency;
// Degrees per second; keeps a sudden change of the target from flinging the cape.
constexpr double maxAngularSpeed = 720.0;
// Frame times are not exact multiples of the step (1/60 s is two steps up to rounding).
constexpr double stepTolerance = 1e-9;

} // namespace

QcCapeDrive QcCapeDrive::from(const QcVec3 &velocity, double yaw, const QcEntityPose &pose)
{
    const double radians = qDegreesToRadians(yaw);
    QcCapeDrive drive;
    drive.forwardSpeed = velocity.x * std::sin(radians) - velocity.z * std::cos(radians);
    drive.sideSpeed = velocity.x * std::cos(radians) + velocity.z * std::sin(radians);
    drive.verticalSpeed = velocity.y;
    drive.onGround = pose.onGround;
    drive.inWater = pose.inWater;
    drive.sneaking = pose.sneaking;
    return drive;
}

QcCapeSwing::Angles QcCapeSwing::target(const QcCapeDrive &drive, double time)
{
    if (drive.sneaking && drive.onGround && !drive.inWater) {
        // Lies flat on the bent back.
        return {minLift, 0.0};
    }

    const double forward = std::max(0.0, drive.forwardSpeed);
    const double backward = std::max(0.0, -drive.forwardSpeed);
    double lift = restLift;
    if (drive.inWater && !drive.onGround) {
        lift += forward * waterLiftPerSpeed;
    } else {
        lift += forward * liftPerSpeed + std::max(0.0, forward - QcPhysics::walkSpeed) * sprintLiftPerSpeed;
        lift -= backward * pressPerSpeed;
        lift += std::max(0.0, -drive.verticalSpeed) * fallLiftPerSpeed;
        lift -= std::max(0.0, drive.verticalSpeed) * risePressPerSpeed;
        if (drive.onGround) {
            lift = std::max(lift, backstepLift * std::min(1.0, backward / QcPhysics::walkSpeed));
        }
    }

    // At rest a slow, gentle sway; on the move a quicker flutter.
    const double moving = std::min(1.0, std::hypot(drive.forwardSpeed, drive.sideSpeed) / flutterSpeed);
    lift += (1.0 - moving) * (2.5 * std::sin(time * 1.3) + std::sin(time * 2.9 + 1.0)) + moving * 2.5 * std::sin(time * 10.0);
    const double sway = drive.sideSpeed * swayPerSpeed + (1.0 - moving) * 1.5 * std::sin(time * 0.9) + moving * 1.5 * std::sin(time * 7.0 + 0.5);
    return {std::clamp(lift, minLift, maxLift), std::clamp(sway, -maxSway, maxSway)};
}

void QcCapeSwing::Axis::step(double target, double low, double high)
{
    const double acceleration = stiffness * (target - angle) - damping * velocity;
    velocity = std::clamp(velocity + acceleration * QcCapeSwing::step, -maxAngularSpeed, maxAngularSpeed);
    angle += velocity * QcCapeSwing::step;
    if (angle < low || angle > high) {
        // Stops at its limits (the back, straight up) instead of passing them.
        angle = std::clamp(angle, low, high);
        velocity = 0.0;
    }
}

void QcCapeSwing::advance(const QcCapeDrive &drive, double seconds, double time)
{
    m_pending += std::clamp(seconds, 0.0, maxAdvance);
    while (m_pending >= step - stepTolerance) {
        m_pending -= step;
        const Angles goal = target(drive, time - m_pending);
        m_lift.step(goal.lift, minLift, maxLift);
        m_sway.step(goal.sway, -maxSway, maxSway);
    }
}

void QcCapeSwing::settle(const QcCapeDrive &drive, double time)
{
    const Angles goal = target(drive, time);
    m_lift = {goal.lift, 0.0};
    m_sway = {goal.sway, 0.0};
    m_pending = 0.0;
}
