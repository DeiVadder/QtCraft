#pragma once

#include "clientcore/input/QcGamepadState.h"

#include <QString>

// How raw stick and trigger values become input (decision E-540): a radial dead zone around the
// centre (the stick's direction is kept, only its length shrinks), an outer zone that already
// counts as full deflection (worn sticks do not reach 1), and a response curve (length to the
// power `exponent`: 1 linear, above 1 finer control near the centre). The output length rises
// continuously from 0 at the dead zone to 1 at the outer zone. Pure functions.
struct QcGamepadResponse
{
    // Share of the stick's travel ignored around the centre, 0..0.9.
    float deadZone = 0.15f;
    // Share of the travel at the rim that already counts as full deflection, 0..0.5, and at most
    // 1 - 0.05 - the larger dead zone; shared by sticks and triggers.
    float outerZone = 0.05f;
    // Exponent of the response curve, 0.2..5.
    float exponent = 1.5f;
    // Share of a trigger's travel ignored at rest, 0..0.9; the triggers share the outer zone and
    // the exponent with the sticks.
    float triggerDeadZone = 0.05f;

    // The values clamped to their ranges (settings may hold anything).
    QcGamepadResponse sanitized() const;

    bool operator==(const QcGamepadResponse &other) const = default;
};

struct QcStickVector
{
    float x = 0.0f;
    float y = 0.0f;

    bool operator==(const QcStickVector &other) const = default;
};

namespace QcGamepadCurve {
// A stick through the radial dead zone and the curve; the result's length is at most 1.
QcStickVector stick(QcStickVector raw, QcGamepadResponse response);
// A trigger (0..1) through its dead zone, the outer zone and the curve.
float trigger(float raw, QcGamepadResponse response);
// Both sticks and triggers of a state, buttons unchanged.
QcGamepadState apply(const QcGamepadState &raw, QcGamepadResponse response);
}
