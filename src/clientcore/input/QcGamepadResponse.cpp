#include "clientcore/input/QcGamepadResponse.h"

#include <algorithm>
#include <cmath>

namespace {

// The least share of the travel between dead and outer zone, so the curve always has a range.
constexpr float minimumSpan = 0.05f;

float clampedOr(float value, float low, float high, float fallback)
{
    return std::isnan(value) ? fallback : std::clamp(value, low, high);
}

// A length 0..1 (or more) through the zones and the curve: 0 up to `dead`, 1 from `1 - outer` on.
float shaped(float length, float dead, float outer, float exponent)
{
    const float span = 1.0f - dead - outer;
    if (length <= dead || span <= 0.0f) {
        return length <= dead ? 0.0f : 1.0f;
    }

    const float scaled = std::min((length - dead) / span, 1.0f);
    return std::pow(scaled, exponent);
}

} // namespace

QcGamepadResponse QcGamepadResponse::sanitized() const
{
    const QcGamepadResponse defaults;
    QcGamepadResponse result;
    result.deadZone = clampedOr(deadZone, 0.0f, 0.9f, defaults.deadZone);
    result.outerZone = clampedOr(outerZone, 0.0f, 0.5f, defaults.outerZone);
    result.exponent = clampedOr(exponent, 0.2f, 5.0f, defaults.exponent);
    result.triggerDeadZone = clampedOr(triggerDeadZone, 0.0f, 0.9f, defaults.triggerDeadZone);

    // The outer zone is shared by sticks and triggers and leaves room for the curve after both
    // dead zones.
    const float largestDeadZone = std::max(result.deadZone, result.triggerDeadZone);
    result.outerZone = std::min(result.outerZone, 1.0f - largestDeadZone - minimumSpan);
    return result;
}

QcStickVector QcGamepadCurve::stick(QcStickVector raw, QcGamepadResponse response)
{
    const QcGamepadResponse r = response.sanitized();
    // NaN counts as centred, anything beyond the travel (even infinite) as the rim.
    const float x = std::isnan(raw.x) ? 0.0f : std::clamp(raw.x, -1.0f, 1.0f);
    const float y = std::isnan(raw.y) ? 0.0f : std::clamp(raw.y, -1.0f, 1.0f);
    const float length = std::hypot(x, y);
    const float output = shaped(length, r.deadZone, r.outerZone, r.exponent);
    if (output <= 0.0f) {
        return {};
    }

    const float factor = output / length;
    return {x * factor, y * factor};
}

float QcGamepadCurve::trigger(float raw, QcGamepadResponse response)
{
    const QcGamepadResponse r = response.sanitized();
    const float value = std::isnan(raw) ? 0.0f : std::clamp(raw, 0.0f, 1.0f);
    return shaped(value, r.triggerDeadZone, r.outerZone, r.exponent);
}

QcGamepadState QcGamepadCurve::apply(const QcGamepadState &raw, QcGamepadResponse response)
{
    QcGamepadState result = raw;
    const auto applyStick = [&](QcGamepadAxis xAxis, QcGamepadAxis yAxis) {
        const QcStickVector v = stick({raw.axis(xAxis), raw.axis(yAxis)}, response);
        result.setAxis(xAxis, v.x);
        result.setAxis(yAxis, v.y);
    };
    applyStick(QcGamepadAxis::LeftX, QcGamepadAxis::LeftY);
    applyStick(QcGamepadAxis::RightX, QcGamepadAxis::RightY);

    for (const QcGamepadAxis axis : {QcGamepadAxis::LeftTrigger, QcGamepadAxis::RightTrigger}) {
        result.setAxis(axis, trigger(raw.axis(axis), response));
    }
    return result;
}
