#pragma once

#include <QString>

#include <array>
#include <cstddef>
#include <optional>

// The buttons of a gamepad by position, not by label (decision E-540): South is A on an Xbox pad,
// Cross on a PlayStation pad and B on a Switch Pro pad. The triggers are axes.
enum class QcGamepadButton {
    South,
    East,
    West,
    North,
    Back,
    Guide,
    Start,
    LeftStick,
    RightStick,
    LeftShoulder,
    RightShoulder,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    Count
};

// The analogue inputs: sticks in [-1, 1] (x positive to the right, y positive downwards, as SDL and
// most pads report them), triggers in [0, 1].
enum class QcGamepadAxis {
    LeftX,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
    Count
};

// The family of a pad, for button symbols in HUD and help texts.
enum class QcGamepadType {
    Generic,
    Xbox,
    PlayStation,
    SwitchPro,
};

inline constexpr std::size_t qcGamepadButtonCount = static_cast<std::size_t>(QcGamepadButton::Count);
inline constexpr std::size_t qcGamepadAxisCount = static_cast<std::size_t>(QcGamepadAxis::Count);

// One pad's buttons and axes at one moment, normalised and independent of the backend.
class QcGamepadState
{
public:
    bool pressed(QcGamepadButton button) const { return m_buttons[static_cast<std::size_t>(button)]; }
    float axis(QcGamepadAxis axis) const { return m_axes[static_cast<std::size_t>(axis)]; }
    void setPressed(QcGamepadButton button, bool pressed) { m_buttons[static_cast<std::size_t>(button)] = pressed; }
    // Clamps to [-1, 1] for the sticks and [0, 1] for the triggers; NaN counts as 0.
    void setAxis(QcGamepadAxis axis, float value);

    static bool isTrigger(QcGamepadAxis axis) { return axis == QcGamepadAxis::LeftTrigger || axis == QcGamepadAxis::RightTrigger; }

    bool operator==(const QcGamepadState &other) const = default;

private:
    std::array<bool, qcGamepadButtonCount> m_buttons{};
    std::array<float, qcGamepadAxisCount> m_axes{};
};

// A connected pad: the id stays the same while it is connected and is not reused by the source.
struct QcGamepadInfo
{
    int id = 0;
    QString name;
    QcGamepadType type = QcGamepadType::Generic;
    // The pad's model as SDL's GUID (32 hex digits; the same for every pad of one model on one
    // connection type): the key of its stored profile (decision E-611). Empty when unknown.
    QString guid;
    // Whether the pad can vibrate (decision E-615).
    bool rumble = false;

    bool operator==(const QcGamepadInfo &other) const = default;
};

// The names of buttons, axes and types in the control interface and in settings ("south",
// "leftX", "playstation"); the lookups return nullopt for an unknown name.
namespace QcGamepadNames {
QString button(QcGamepadButton button);
QString axis(QcGamepadAxis axis);
QString type(QcGamepadType type);
std::optional<QcGamepadButton> buttonFromName(const QString &name);
std::optional<QcGamepadAxis> axisFromName(const QString &name);
std::optional<QcGamepadType> typeFromName(const QString &name);
}
