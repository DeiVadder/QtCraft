// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include <array>
#include <cstddef>
#include <optional>

/*! \brief The buttons of a gamepad by position, not by label (decision E-540): South is A on an
    Xbox pad, Cross on a PlayStation pad and B on a Switch Pro pad.

    The triggers are axes. */
enum class QcGamepadButton {
    South,         // bottom face button
    East,          // right face button
    West,          // left face button
    North,         // top face button
    Back,          // the left middle button (Back, Share, Minus)
    Guide,         // the logo button
    Start,         // the right middle button (Start, Options, Plus)
    LeftStick,     // pressing the left stick
    RightStick,    // pressing the right stick
    LeftShoulder,  // left bumper
    RightShoulder, // right bumper
    DpadUp,        // d-pad up
    DpadDown,      // d-pad down
    DpadLeft,      // d-pad left
    DpadRight,     // d-pad right
    Count          // the number of buttons, not a button
};

/*! \brief The analogue inputs: sticks in [-1, 1] (x positive to the right, y positive downwards,
    as SDL and most pads report them), triggers in [0, 1]. */
enum class QcGamepadAxis {
    LeftX,        // left stick, horizontal
    LeftY,        // left stick, vertical
    RightX,       // right stick, horizontal
    RightY,       // right stick, vertical
    LeftTrigger,  // left trigger
    RightTrigger, // right trigger
    Count         // the number of axes, not an axis
};

/*! \brief The family of a pad, for button symbols in HUD and help texts. */
enum class QcGamepadType {
    Generic,     // unknown family: generic symbols
    Xbox,        // Xbox and compatible pads
    PlayStation, // PlayStation pads
    SwitchPro,   // Nintendo Switch Pro pads
};

/*! Number of buttons. */
inline constexpr std::size_t qcGamepadButtonCount = static_cast<std::size_t>(QcGamepadButton::Count);
/*! Number of axes. */
inline constexpr std::size_t qcGamepadAxisCount = static_cast<std::size_t>(QcGamepadAxis::Count);

/*! \brief One pad's buttons and axes at one moment, normalised and independent of the backend. */
class QcGamepadState
{
public:
    /*! Returns whether \a button is pressed. */
    bool pressed(QcGamepadButton button) const { return m_buttons[static_cast<std::size_t>(button)]; }
    /*! Returns the value of \a axis. */
    float axis(QcGamepadAxis axis) const { return m_axes[static_cast<std::size_t>(axis)]; }
    /*! Sets whether \a button is \a pressed. */
    void setPressed(QcGamepadButton button, bool pressed) { m_buttons[static_cast<std::size_t>(button)] = pressed; }
    /*! Sets \a axis to \a value, clamped to [-1, 1] for the sticks and [0, 1] for the triggers;
        NaN counts as 0. */
    void setAxis(QcGamepadAxis axis, float value);

    /*! Returns whether \a axis is a trigger (range [0, 1]). */
    static bool isTrigger(QcGamepadAxis axis) { return axis == QcGamepadAxis::LeftTrigger || axis == QcGamepadAxis::RightTrigger; }

    bool operator==(const QcGamepadState &other) const = default;

private:
    std::array<bool, qcGamepadButtonCount> m_buttons{};
    std::array<float, qcGamepadAxisCount> m_axes{};
};

/*! \brief A connected pad: the id stays the same while it is connected and is not reused by the
    source. */
struct QcGamepadInfo
{
    /*! The pad's id while it is connected. */
    int id = 0;
    /*! The pad's name as the backend reports it. */
    QString name;
    /*! The pad's family. */
    QcGamepadType type = QcGamepadType::Generic;
    /*! The pad's model as SDL's GUID (32 hex digits; the same for every pad of one model on one
        connection type): the key of its stored profile (decision E-611). Empty when unknown. */
    QString guid;
    /*! Whether the pad can vibrate (decision E-615). */
    bool rumble = false;

    bool operator==(const QcGamepadInfo &other) const = default;
};

/*! \brief The names of buttons, axes and types in the control interface and in settings
    ("south", "leftX", "playstation"); the lookups return nullopt for an unknown name. */
namespace QcGamepadNames {
/*! Returns the name of \a button. */
QString button(QcGamepadButton button);
/*! Returns the name of \a axis. */
QString axis(QcGamepadAxis axis);
/*! Returns the name of \a type. */
QString type(QcGamepadType type);
/*! Returns the button named \a name. */
std::optional<QcGamepadButton> buttonFromName(const QString &name);
/*! Returns the axis named \a name. */
std::optional<QcGamepadAxis> axisFromName(const QString &name);
/*! Returns the pad type named \a name. */
std::optional<QcGamepadType> typeFromName(const QString &name);
}
