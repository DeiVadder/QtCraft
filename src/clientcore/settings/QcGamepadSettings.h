#pragma once

#include "clientcore/input/QcGamepadResponse.h"

class QSettings;

// How a gamepad steers the player (decision E-541): dead zones, outer zone and response curve of
// sticks and triggers (QcGamepadResponse), the turning speed of the right stick at full deflection
// and whether pushing it up looks down. Stored in settings.ini as gamepad/<key>; values out of
// range are clamped, unusable ones give the defaults.
struct QcGamepadSettings
{
    static constexpr double defaultLookSpeed = 180.0;
    static constexpr double minLookSpeed = 30.0;
    static constexpr double maxLookSpeed = 720.0;

    QcGamepadResponse response;
    // Degrees per second at full deflection of the right stick.
    double lookSpeed = defaultLookSpeed;
    bool invertLook = false;

    // Clamps to minLookSpeed..maxLookSpeed; NaN and infinities give the default.
    static double clampLookSpeed(double degreesPerSecond);

    static QcGamepadSettings load(const QSettings &file);
    void save(QSettings &file) const;

    friend bool operator==(const QcGamepadSettings &, const QcGamepadSettings &) = default;
};
