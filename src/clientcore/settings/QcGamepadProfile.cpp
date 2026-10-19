#include "clientcore/settings/QcGamepadProfile.h"

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

// The buttons stored as "<prefix>/bind/<action>" = button name; unknown names keep the default.
QcGamepadMapping::Bindings loadBindings(const QSettings &file, const QString &prefix)
{
    using QcGamepadMapping::Action;
    QcGamepadMapping::Bindings bindings;
    for (std::size_t index = 0; index < QcGamepadMapping::actionCount; ++index) {
        const auto action = Action(index);
        if (!QcGamepadMapping::rebindable(action)) {
            continue;
        }
        const QString name = file.value(u"%1/bind/%2"_s.arg(prefix, QcGamepadMapping::actionName(action))).toString();
        if (const std::optional<QcGamepadButton> button = QcGamepadNames::buttonFromName(name)) {
            bindings.setButton(action, *button);
        }
    }
    return bindings;
}

} // namespace

double QcGamepadProfile::clampLookSpeed(double degreesPerSecond)
{
    return std::isfinite(degreesPerSecond) ? std::clamp(degreesPerSecond, minLookSpeed, maxLookSpeed) : defaultLookSpeed;
}

float QcGamepadProfile::clampTriggerThreshold(float share)
{
    return std::isnan(share) ? defaultTriggerThreshold : std::clamp(share, minTriggerThreshold, maxTriggerThreshold);
}

QcGamepadProfile QcGamepadProfile::sanitized() const
{
    QcGamepadProfile result = *this;
    result.response = response.sanitized();
    result.lookSpeed = clampLookSpeed(lookSpeed);
    result.triggerThreshold = clampTriggerThreshold(triggerThreshold);
    if (!bindings.conflicts().isEmpty()) {
        result.bindings = {};
    }
    return result;
}

QcGamepadProfile QcGamepadProfile::load(const QSettings &file, const QString &prefix)
{
    const QcGamepadProfile defaults;
    QcGamepadProfile profile;
    profile.response.deadZone = storedFloat(file, prefix + u"/deadZone"_s, defaults.response.deadZone);
    profile.response.outerZone = storedFloat(file, prefix + u"/outerZone"_s, defaults.response.outerZone);
    profile.response.exponent = storedFloat(file, prefix + u"/curve"_s, defaults.response.exponent);
    profile.response.triggerDeadZone = storedFloat(file, prefix + u"/triggerDeadZone"_s, defaults.response.triggerDeadZone);

    bool ok = false;
    const double speed = file.value(prefix + u"/lookSpeed"_s).toDouble(&ok);
    profile.lookSpeed = ok ? speed : defaultLookSpeed;
    profile.invertLook = file.value(prefix + u"/invertLook"_s, false).toBool();
    profile.triggerThreshold = storedFloat(file, prefix + u"/triggerThreshold"_s, defaultTriggerThreshold);
    profile.vibration = file.value(prefix + u"/vibration"_s, true).toBool();
    profile.bindings = loadBindings(file, prefix);
    return profile.sanitized();
}

void QcGamepadProfile::save(QSettings &file, const QString &prefix) const
{
    file.setValue(prefix + u"/deadZone"_s, response.deadZone);
    file.setValue(prefix + u"/outerZone"_s, response.outerZone);
    file.setValue(prefix + u"/curve"_s, response.exponent);
    file.setValue(prefix + u"/triggerDeadZone"_s, response.triggerDeadZone);
    file.setValue(prefix + u"/lookSpeed"_s, lookSpeed);
    file.setValue(prefix + u"/invertLook"_s, invertLook);
    file.setValue(prefix + u"/triggerThreshold"_s, triggerThreshold);
    file.setValue(prefix + u"/vibration"_s, vibration);
    for (std::size_t index = 0; index < QcGamepadMapping::actionCount; ++index) {
        const auto action = QcGamepadMapping::Action(index);
        if (const std::optional<QcGamepadButton> button = bindings.button(action)) {
            file.setValue(u"%1/bind/%2"_s.arg(prefix, QcGamepadMapping::actionName(action)), QcGamepadNames::button(*button));
        }
    }
}
