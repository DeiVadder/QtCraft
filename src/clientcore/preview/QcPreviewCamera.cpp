// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/preview/QcPreviewCamera.h"

#include "clientcore/debug/QcClientLogging.h"

#include <algorithm>
#include <cmath>

namespace {

double wrapDegrees(double degrees)
{
    const double wrapped = std::fmod(degrees, 360.0);
    return wrapped < 0.0 ? wrapped + 360.0 : wrapped;
}

} // namespace

void QcPreviewCamera::setModelYaw(double yaw)
{
    if (!std::isfinite(yaw)) {
        qCWarning(lcPreviewCamera) << "Preview camera: yaw" << yaw << "refused: not finite";
        return;
    }
    m_modelYaw = wrapDegrees(yaw);
}

void QcPreviewCamera::setZoom(double zoom)
{
    if (!std::isfinite(zoom)) {
        qCWarning(lcPreviewCamera) << "Preview camera: zoom" << zoom << "refused: not finite";
        return;
    }
    // Beyond the range is a wanted overshoot (a wheel turned past the end): clamped silently.
    m_zoom = std::clamp(zoom, 0.0, 1.0);
}

void QcPreviewCamera::drag(double pixels)
{
    // The camera looks along -Z: turning the yaw down moves the figure's front to the right.
    setModelYaw(modelYaw() - pixels * degreesPerPixel);
}

void QcPreviewCamera::wheel(int angleDelta)
{
    setZoom(zoom() + zoomPerStep * static_cast<double>(angleDelta) / 120.0);
}

void QcPreviewCamera::show(Side side)
{
    setModelYaw(side == Side::Front ? frontYaw : 0.0);
}

QcPreviewCamera::Side QcPreviewCamera::side() const
{
    const double away = std::abs(wrapDegrees(modelYaw() - frontYaw + 180.0) - 180.0);
    return away < 90.0 ? Side::Front : Side::Back;
}

void QcPreviewCamera::reset()
{
    setModelYaw(frontYaw + defaultTurn);
    setZoom(0.0);
}

double QcPreviewCamera::distance() const
{
    return farDistance + (nearDistance - farDistance) * zoom();
}

QcVec3 QcPreviewCamera::target() const
{
    return {0.0, bodyHeight + (headHeight - bodyHeight) * zoom(), 0.0};
}

QcCamera QcPreviewCamera::camera() const
{
    const QcVec3 at = target();
    QcCamera camera;
    camera.position = {at.x, at.y, at.z + distance()};
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    camera.fieldOfView = fieldOfView;
    return camera;
}
