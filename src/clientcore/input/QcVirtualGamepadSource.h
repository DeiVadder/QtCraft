#pragma once

#include "clientcore/input/QcGamepadSource.h"

// Gamepads fed from outside (decision E-540): unit tests and the control interface
// (gamepadConnect, gamepadSet, ...) connect pads and set their buttons and axes; the input
// mapping cannot tell them from real ones. Always available.
class QcVirtualGamepadSource : public QcGamepadSource
{
    Q_OBJECT

public:
    using QcGamepadSource::QcGamepadSource;

    QString backendName() const override;
    bool isAvailable() const override { return true; }

    // Connects a pad with everything released; returns its id.
    int connectPad(const QString &name, QcGamepadType type);
    // False for an unknown id.
    bool disconnectPad(int id);
    bool setState(int id, const QcGamepadState &state);
    bool setPressed(int id, QcGamepadButton button, bool pressed);
    // Clamped like QcGamepadState::setAxis.
    bool setAxis(int id, QcGamepadAxis axis, float value);
};
