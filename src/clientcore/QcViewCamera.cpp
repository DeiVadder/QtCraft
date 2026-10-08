#include "QcViewCamera.h"

#include "QcRaycast.h"

#include <QString>
#include <QtMath>

#include <algorithm>
#include <cmath>

namespace QcViewCamera {

Placement place(const QcVec3 &eye, float yaw, float pitch, QcCameraMode mode, const QcBlockAccess &world)
{
    if (mode == QcCameraMode::FirstPerson) {
        return {eye, yaw, pitch};
    }
    const double y = qDegreesToRadians(double(yaw));
    const double p = qDegreesToRadians(double(pitch));
    const QcVec3 forward{std::sin(y) * std::cos(p), std::sin(p), -std::cos(y) * std::cos(p)};
    // Behind: back along the view direction; front: ahead of the player, looking back at them.
    const QcVec3 direction = mode == QcCameraMode::Behind ? forward * -1.0 : forward;
    double distance = thirdPersonDistance;
    if (const std::optional<QcRaycastHit> hit = QcRaycast::cast(world, eye, direction, thirdPersonDistance + wallMargin,
                                                                             QcRaycast::Targets::Solid)) {
        distance = std::clamp(hit->distance - wallMargin, 0.0, thirdPersonDistance);
    }
    const QcVec3 position = eye + direction * distance;
    if (mode == QcCameraMode::Behind) {
        return {position, yaw, pitch};
    }
    return {position, float(std::fmod(double(yaw) + 180.0, 360.0)), -pitch};
}

QcCameraMode next(QcCameraMode mode)
{
    switch (mode) {
    case QcCameraMode::FirstPerson:
        return QcCameraMode::Behind;
    case QcCameraMode::Behind:
        return QcCameraMode::Front;
    case QcCameraMode::Front:
        break;
    }
    return QcCameraMode::FirstPerson;
}

const char *name(QcCameraMode mode)
{
    switch (mode) {
    case QcCameraMode::Behind:
        return "behind";
    case QcCameraMode::Front:
        return "front";
    case QcCameraMode::FirstPerson:
        break;
    }
    return "first";
}

std::optional<QcCameraMode> fromName(const QString &text)
{
    for (const QcCameraMode mode : {QcCameraMode::FirstPerson, QcCameraMode::Behind, QcCameraMode::Front}) {
        if (text == QLatin1StringView(name(mode))) {
            return mode;
        }
    }
    return std::nullopt;
}

} // namespace QcViewCamera
