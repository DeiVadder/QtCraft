#include "QcMouseLook.h"

#include <algorithm>
#include <cmath>

namespace QcMouseLook {

QcPlayerInput apply(QcPlayerInput intent, QPointF delta, double sensitivity, bool invert)
{
    if (!std::isfinite(delta.x()) || !std::isfinite(delta.y())) {
        return intent;
    }
    const auto horizontal = float(sensitivity);
    const float vertical = invert ? -horizontal : horizontal;
    const float yaw = std::fmod(intent.yaw + float(delta.x()) * horizontal, 360.0f);
    const float wrapped = yaw < 0.0f ? yaw + 360.0f : yaw;
    // A tiny negative yaw rounds to exactly 360 after the addition; the range is [0, 360).
    intent.yaw = wrapped >= 360.0f ? 0.0f : wrapped;
    intent.pitch = std::clamp(intent.pitch - float(delta.y()) * vertical, -89.9f, 89.9f);
    return intent;
}

} // namespace QcMouseLook
