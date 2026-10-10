#include "QcPreviewCamera.h"

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
    if (std::isfinite(yaw)) {
        m_modelYaw = wrapDegrees(yaw);
    }
}

void QcPreviewCamera::setZoom(double zoom)
{
    if (std::isfinite(zoom)) {
        m_zoom = std::clamp(zoom, 0.0, 1.0);
    }
}

void QcPreviewCamera::drag(double pixels)
{
    // The camera looks along -Z: turning the yaw down moves the figure's front to the right.
    setModelYaw(m_modelYaw - pixels * degreesPerPixel);
}

void QcPreviewCamera::wheel(int angleDelta)
{
    setZoom(m_zoom + zoomPerStep * double(angleDelta) / 120.0);
}

void QcPreviewCamera::show(Side side)
{
    m_modelYaw = side == Side::Front ? frontYaw : 0.0;
}

QcPreviewCamera::Side QcPreviewCamera::side() const
{
    const double away = std::abs(wrapDegrees(m_modelYaw - frontYaw + 180.0) - 180.0);
    return away < 90.0 ? Side::Front : Side::Back;
}

void QcPreviewCamera::reset()
{
    m_modelYaw = frontYaw + defaultTurn;
    m_zoom = 0.0;
}

double QcPreviewCamera::distance() const
{
    return farDistance + (nearDistance - farDistance) * m_zoom;
}

QcVec3 QcPreviewCamera::target() const
{
    return {0.0, bodyHeight + (headHeight - bodyHeight) * m_zoom, 0.0};
}

QcCamera QcPreviewCamera::camera() const
{
    const QcVec3 at = target();
    QcCamera camera;
    camera.position = QVector3D(float(at.x), float(at.y), float(at.z + distance()));
    camera.yaw = 0.0f;
    camera.pitch = 0.0f;
    camera.fieldOfView = fieldOfView;
    return camera;
}
