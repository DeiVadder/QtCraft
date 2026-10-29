// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/preview/QcPreviewPose.h"

#include "clientcore/preview/QcPreviewCamera.h"
#include "common/physics/QcPhysics.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace QcPreviewPose {

namespace {

// Where the humanoid model's eyes look from (half way up its head).
constexpr double eyeHeight = 1.58;
// Share of the way from the figure to the camera where the pointer is taken.
constexpr double gazePlane = 1.0 / 3.0;

double degrees(double radians)
{
    return qRadiansToDegrees(radians);
}

} // namespace

QString name(Animation animation)
{
    switch (animation) {
    case Animation::Walk:
        return u"walk"_s;
    case Animation::Mine:
        return u"mine"_s;
    case Animation::Stand:
        break;
    }
    return u"stand"_s;
}

[[nodiscard]] std::optional<Animation> fromName(const QString &name)
{
    for (const Animation animation : {Animation::Stand, Animation::Walk, Animation::Mine}) {
        if (QcPreviewPose::name(animation) == name) {
            return animation;
        }
    }
    return std::nullopt;
}

QcEntityPose pose(Animation animation, double seconds, Gaze gaze)
{
    QcEntityPose pose;
    pose.onGround = true;
    pose.headYaw = static_cast<float>(gaze.yaw);
    pose.headPitch = static_cast<float>(gaze.pitch);
    if (animation == Animation::Walk) {
        pose.speed = QcPhysics::walkSpeed;
        pose.walkDistance = seconds * QcPhysics::walkSpeed;
    } else if (animation == Animation::Mine) {
        const double sinceSwing = std::fmod(std::max(0.0, seconds), swingPeriod);
        pose.swingProgress = sinceSwing < swingSeconds ? sinceSwing / swingSeconds : -1.0;
    }
    return pose;
}

QcCapeDrive capeDrive(Animation animation)
{
    QcCapeDrive drive;
    drive.forwardSpeed = animation == Animation::Walk ? QcPhysics::walkSpeed : 0.0;
    return drive;
}

Gaze gazeAt(QcPreviewCamera camera, double pointerX, double pointerY, double aspect)
{
    // The pointer's ray from the camera, taken where it crosses a plane a third of the way from
    // the figure to the camera: the head turns clearly towards the pointer, about straight ahead
    // when it is in the middle.
    const double distance = camera.distance();
    const QcVec3 eye = camera.target() + QcVec3{0.0, 0.0, distance};
    const double slope = std::tan(qDegreesToRadians(QcPreviewCamera::fieldOfView / 2.0));
    const QcVec3 direction{std::clamp(pointerX, -1.0, 1.0) * slope * aspect, std::clamp(pointerY, -1.0, 1.0) * slope, -1.0};
    const QcVec3 pointer = eye + direction * (distance * (1.0 - gazePlane));

    // Into the figure's frame (facing -Z, +X to its right) from its eyes.
    const QcVec3 seen = pointer - QcVec3{0.0, eyeHeight, 0.0};
    const double yaw = qDegreesToRadians(camera.modelYaw());
    const QcVec3 local{seen.x * std::cos(yaw) + seen.z * std::sin(yaw), seen.y, -seen.x * std::sin(yaw) + seen.z * std::cos(yaw)};

    const double ahead = -local.z;
    const double side = local.x;
    double turn = degrees(std::atan2(side, ahead));
    // Behind the figure: the further back, the more the head returns to the front.
    if (std::abs(turn) > 90.0) {
        turn = std::copysign(maxGazeYaw * (180.0 - std::abs(turn)) / 90.0, turn);
    }
    const double tilt = degrees(std::atan2(local.y, std::hypot(side, ahead)));
    return {std::clamp(turn, -maxGazeYaw, maxGazeYaw), std::clamp(tilt, -maxGazePitch, maxGazePitch)};
}

Gaze approach(Gaze current, Gaze target, double seconds)
{
    const double share = 1.0 - std::exp(-gazeRate * std::max(0.0, seconds));
    return {current.yaw + (target.yaw - current.yaw) * share, current.pitch + (target.pitch - current.pitch) * share};
}

} // namespace QcPreviewPose
