// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/physics/QcPose.h"

#include "common/physics/QcPhysics.h"

namespace QcPoses {

QcAabb boxAt(QcPose pose, const QcVec3 &feet, float yaw, double width, double height)
{
    return pose == QcPose::Lying ? QcPhysics::lyingBoxAt(feet, yaw, width, height) : QcPhysics::boxAt(feet, width, height);
}

QcVec3 eyesAt(QcPose pose, const QcVec3 &feet, float yaw, double width, double height)
{
    if (pose == QcPose::Lying) {
        const QcAabb lying = QcPhysics::lyingBoxAt(feet, yaw, width, height);
        return (lying.min + lying.max) * 0.5;
    }
    return feet + QcVec3{0.0, height * eyeShare, 0.0};
}

} // namespace QcPoses
