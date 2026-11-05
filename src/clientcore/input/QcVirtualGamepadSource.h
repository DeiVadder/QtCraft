// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/input/QcGamepadSource.h"

#include <QHash>
#include <QList>

/*! \brief Gamepads fed from outside (decision E-540): unit tests and the control interface
    (gamepadConnect, gamepadSet, ...) connect pads and set their buttons and axes; the input mapping
    cannot tell them from real ones.

    Always available; every virtual pad can vibrate, and the vibrations asked for are kept for
    tests. */
class QcVirtualGamepadSource : public QcGamepadSource
{
    Q_OBJECT

public:
    using QcGamepadSource::QcGamepadSource;

    QString backendName() const override;
    bool isAvailable() const override { return true; }

    /*! \brief A vibration asked for (rumble()). */
    struct Rumble
    {
        /*! Strength, 0..1. */
        float strength = 0.0f;
        /*! Duration in milliseconds. */
        int durationMs = 0;
    };

    /*! Connects a pad called \a name of the family \a type with everything released; returns its
        id. \a guid names its model (profiles). */
    int connectPad(const QString &name, QcGamepadType type, const QString &guid = {});
    /*! Disconnects pad \a id. Returns false for an unknown id. */
    bool disconnectPad(int id);
    /*! Sets the buttons and axes of pad \a id to \a state. Returns false for an unknown id. */
    bool setState(int id, const QcGamepadState &state);
    /*! Sets whether \a button of pad \a id is \a pressed. Returns false for an unknown id. */
    bool setPressed(int id, QcGamepadButton button, bool pressed);
    /*! Sets \a axis of pad \a id to \a value, clamped like QcGamepadState::setAxis. Returns
        false for an unknown id. */
    bool setAxis(int id, QcGamepadAxis axis, float value);

    bool rumble(int id, float strength, int durationMs) override;
    /*! Returns the vibrations asked of pad \a id since it connected, oldest first. */
    QList<Rumble> rumbles(int id) const { return m_rumbles.value(id); }

private:
    QHash<int, QList<Rumble>> m_rumbles;
};

Q_DECLARE_TYPEINFO(QcVirtualGamepadSource::Rumble, Q_PRIMITIVE_TYPE);
