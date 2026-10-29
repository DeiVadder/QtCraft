// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/input/QcGamepadSource.h"

#include <QHash>
#include <QList>

// Gamepads fed from outside (decision E-540): unit tests and the control interface
// (gamepadConnect, gamepadSet, ...) connect pads and set their buttons and axes; the input
// mapping cannot tell them from real ones. Always available; every virtual pad can vibrate, and
// the vibrations asked for are kept for tests.
class QcVirtualGamepadSource : public QcGamepadSource
{
    Q_OBJECT

public:
    using QcGamepadSource::QcGamepadSource;

    QString backendName() const override;
    bool isAvailable() const override { return true; }

    // A vibration asked for (rumble()).
    struct Rumble
    {
        float strength = 0.0f;
        int durationMs = 0;
    };

    // Connects a pad with everything released; returns its id. `guid` names its model (profiles).
    int connectPad(const QString &name, QcGamepadType type, const QString &guid = {});
    // False for an unknown id.
    bool disconnectPad(int id);
    bool setState(int id, const QcGamepadState &state);
    bool setPressed(int id, QcGamepadButton button, bool pressed);
    // Clamped like QcGamepadState::setAxis.
    bool setAxis(int id, QcGamepadAxis axis, float value);

    bool rumble(int id, float strength, int durationMs) override;
    // The vibrations asked of pad `id` since it connected, oldest first.
    QList<Rumble> rumbles(int id) const { return m_rumbles.value(id); }

private:
    QHash<int, QList<Rumble>> m_rumbles;
};

Q_DECLARE_TYPEINFO(QcVirtualGamepadSource::Rumble, Q_PRIMITIVE_TYPE);
