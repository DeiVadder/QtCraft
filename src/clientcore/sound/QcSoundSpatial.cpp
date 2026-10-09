#include "QcSoundSpatial.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace {

// Horizontal unit direction from the listener to the source; zero when the source is (almost)
// straight above or below.
QcVec3 horizontalDirection(const QcVec3 &listener, const QcVec3 &source)
{
    const double dx = source.x - listener.x;
    const double dz = source.z - listener.z;
    const double length = std::hypot(dx, dz);
    if (length < 0.01) {
        return {};
    }
    return {dx / length, 0.0, dz / length};
}

} // namespace

float QcSoundSpatial::attenuation(double distance, double range)
{
    if (!std::isfinite(distance) || !(range > 0.0) || distance >= range) {
        return 0.0f;
    }

    const double closeness = 1.0 - std::max(0.0, distance) / range;
    return float(closeness * closeness);
}

float QcSoundSpatial::pan(const QcVec3 &listener, double yawDegrees, const QcVec3 &source)
{
    const QcVec3 direction = horizontalDirection(listener, source);
    const double yaw = qDegreesToRadians(yawDegrees);
    // Right-hand side of a listener looking along (sin yaw, 0, -cos yaw).
    const double right = direction.x * std::cos(yaw) + direction.z * std::sin(yaw);
    return float(right) * panWidth;
}

QcSoundSpatial::Placement QcSoundSpatial::place(const QcVec3 &listener, double yawDegrees, const QcVec3 &source, double range)
{
    Placement placement;
    placement.distance = (source - listener).length();
    const QcVec3 direction = horizontalDirection(listener, source);
    const double yaw = qDegreesToRadians(yawDegrees);
    const double ahead = direction.x * std::sin(yaw) - direction.z * std::cos(yaw);
    const double behind = std::max(0.0, -ahead);
    placement.gain = attenuation(placement.distance, range) * float(1.0 - rearDamping * behind);
    placement.pan = pan(listener, yawDegrees, source);
    return placement;
}
