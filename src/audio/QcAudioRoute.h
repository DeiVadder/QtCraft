#pragma once

#include <QByteArray>
#include <QList>

// Which output device the game should play through when the system's devices change (a headset
// switched on, a speaker unplugged): the system's default output, as long as there is one. Pure,
// on device ids only, so that it can be checked without real devices.
class QcAudioRoute
{
public:
    struct Decision
    {
        enum class Action { Keep, Switch, Close };

        Action action = Action::Keep;
        // The device to switch to (Switch only).
        QByteArray device;

        friend bool operator==(const Decision &, const Decision &) = default;
    };

    // `current`: the device playing now, empty when none is open; `outputs`: the devices there
    // are; `defaultOutput`: the system's default output, empty when it has none. Switches to the
    // default when it changed, when the current device went away, and when a device appeared while
    // none was open; closes when the current device went away and none is left.
    [[nodiscard]] static Decision decide(const QByteArray &current, const QList<QByteArray> &outputs, const QByteArray &defaultOutput);
};
