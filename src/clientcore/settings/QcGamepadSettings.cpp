#include "clientcore/settings/QcGamepadSettings.h"

#include <QSettings>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// The stored number of `key`, or `fallback` when it is missing or not a number.
float storedFloat(const QSettings &file, const QString &key, float fallback)
{
    bool ok = false;
    const float value = file.value(key).toFloat(&ok);
    return ok ? value : fallback;
}

} // namespace

double QcGamepadSettings::clampLookSpeed(double degreesPerSecond)
{
    return std::isfinite(degreesPerSecond) ? std::clamp(degreesPerSecond, minLookSpeed, maxLookSpeed) : defaultLookSpeed;
}

QcGamepadSettings QcGamepadSettings::load(const QSettings &file)
{
    const QcGamepadSettings defaults;
    QcGamepadSettings settings;
    QcGamepadResponse response;
    response.deadZone = storedFloat(file, u"gamepad/deadZone"_s, defaults.response.deadZone);
    response.outerZone = storedFloat(file, u"gamepad/outerZone"_s, defaults.response.outerZone);
    response.exponent = storedFloat(file, u"gamepad/curve"_s, defaults.response.exponent);
    response.triggerDeadZone = storedFloat(file, u"gamepad/triggerDeadZone"_s, defaults.response.triggerDeadZone);
    settings.response = response.sanitized();

    bool ok = false;
    const double speed = file.value(u"gamepad/lookSpeed"_s).toDouble(&ok);
    settings.lookSpeed = ok ? clampLookSpeed(speed) : defaultLookSpeed;
    settings.invertLook = file.value(u"gamepad/invertLook"_s, false).toBool();
    return settings;
}

void QcGamepadSettings::save(QSettings &file) const
{
    file.setValue(u"gamepad/deadZone"_s, response.deadZone);
    file.setValue(u"gamepad/outerZone"_s, response.outerZone);
    file.setValue(u"gamepad/curve"_s, response.exponent);
    file.setValue(u"gamepad/triggerDeadZone"_s, response.triggerDeadZone);
    file.setValue(u"gamepad/lookSpeed"_s, lookSpeed);
    file.setValue(u"gamepad/invertLook"_s, invertLook);
}
