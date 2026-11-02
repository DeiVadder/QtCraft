// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/input/QcGamepadMapping.h"
#include "clientcore/input/QcGamepadResponse.h"

#include <QString>

class QSettings;

/*! \brief How one pad steers the player (decisions E-541, E-610, E-611): dead zones, outer zone and
    response curve of sticks and triggers (QcGamepadResponse), the turning speed of the right stick
    at full deflection, whether pushing it up looks down, how far a trigger is pulled before it
    acts, whether the pad vibrates and the buttons of the actions.

    The settings keep one for all pads and one per pad model that has its own
    (QcGamepadSettings). Stored below a key prefix ("gamepad/<key>"); values out of range are
    clamped, unusable ones give the defaults. */
struct QcGamepadProfile
{
    /*! Turning speed of a new profile, in degrees per second. */
    static constexpr double defaultLookSpeed = 180.0;
    /*! Slowest turning speed that can be set. */
    static constexpr double minLookSpeed = 30.0;
    /*! Fastest turning speed that can be set. */
    static constexpr double maxLookSpeed = 720.0;
    /*! Trigger threshold of a new profile: the share of the travel at which a press counts. */
    static constexpr float defaultTriggerThreshold = QcGamepadMapping::pressThreshold;
    /*! Lowest trigger threshold that can be set. */
    static constexpr float minTriggerThreshold = 0.1f;
    /*! Highest trigger threshold that can be set. */
    static constexpr float maxTriggerThreshold = 0.9f;

    /*! Dead zones, outer zone and response curve of sticks and triggers. */
    QcGamepadResponse response;
    /*! Degrees per second at full deflection of the right stick. */
    double lookSpeed = defaultLookSpeed;
    /*! Whether pushing the right stick up looks down. */
    bool invertLook = false;
    /*! A trigger acts beyond this share of its travel (after dead zone and curve) and lets go
        again 0.2 below it (QcGamepadMapping::releaseFor). */
    float triggerThreshold = defaultTriggerThreshold;
    /*! The pad vibrates when the player is hurt (decision E-615). */
    bool vibration = true;
    /*! The buttons of the actions. */
    QcGamepadMapping::Bindings bindings;

    /*! Returns \a degreesPerSecond clamped to minLookSpeed..maxLookSpeed; NaN and infinities
        give the default. */
    static double clampLookSpeed(double degreesPerSecond);
    /*! Returns \a share clamped to minTriggerThreshold..maxTriggerThreshold; NaN gives the
        default. */
    static float clampTriggerThreshold(float share);
    /*! Returns the profile with every value in its range; a button table in conflict (a file
        edited by hand) gives the default buttons. */
    QcGamepadProfile sanitized() const;

    /*! Returns the profile stored as "<prefix>/<key>" in \a file (\a prefix without the slash);
        missing keys give the defaults. */
    static QcGamepadProfile load(const QSettings &file, const QString &prefix);
    /*! Stores the profile in \a file as "<prefix>/<key>" (\a prefix without the slash). */
    void save(QSettings &file, const QString &prefix) const;

    friend bool operator==(const QcGamepadProfile &, const QcGamepadProfile &) = default;
};
