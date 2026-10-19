#pragma once

#include "clientcore/input/QcGamepadState.h"

#include <QPointF>
#include <QtGlobal>

#include <array>
#include <cstddef>
#include <optional>

// What the pad does in the world and the arithmetic behind it (decision E-541), without state and
// without a session: the default bindings, the walking direction of the left stick, the turn of
// the right stick in one frame, the triggers as buttons and the hotbar steps of the shoulders.
namespace QcGamepadMapping {

// The player's actions on a pad. Mine and Use are the triggers, the others are buttons.
enum class Action {
    Jump,
    Sprint,
    Sneak,
    Mine,
    Use,
    HotbarPrevious,
    HotbarNext,
    // The pause menu, the inventory, the chat line, the handbook, the camera view and the player
    // list (held), as with their keys (QcGamepadControls::worldAction).
    Pause,
    Inventory,
    Chat,
    Handbook,
    CameraView,
    PlayerList,
    // Throws one item; held for dropStackMs, the rest of the stack.
    Drop,
    // The targeted block's item into the hand (the middle mouse button).
    PickBlock,
    // Held: the radial menu of the hotbar, chosen with the right stick (QcHotbarRadial).
    HotbarRadial,
    Count
};
inline constexpr std::size_t actionCount = std::size_t(Action::Count);

// Default bindings (Auftrag point 40, decisions E-541 and E-580, after the console layout of the
// model): south jumps (double tap: creative flight), the left stick click sprints, the right stick
// click sneaks, the shoulders step through the hotbar; start pauses, north opens the inventory,
// east drops, west picks the block in view; the d-pad holds the radial menu (left), switches the
// camera (up), opens the chat (down) and the handbook (right); back holds the player list. The
// triggers (right mines and attacks, left uses) are axes and not in this table.
struct Bindings
{
    QcGamepadButton jump = QcGamepadButton::South;
    QcGamepadButton sprint = QcGamepadButton::LeftStick;
    QcGamepadButton sneak = QcGamepadButton::RightStick;
    QcGamepadButton hotbarPrevious = QcGamepadButton::LeftShoulder;
    QcGamepadButton hotbarNext = QcGamepadButton::RightShoulder;
    QcGamepadButton pause = QcGamepadButton::Start;
    QcGamepadButton inventory = QcGamepadButton::North;
    QcGamepadButton chat = QcGamepadButton::DpadDown;
    QcGamepadButton handbook = QcGamepadButton::DpadRight;
    QcGamepadButton cameraView = QcGamepadButton::DpadUp;
    QcGamepadButton playerList = QcGamepadButton::Back;
    QcGamepadButton drop = QcGamepadButton::East;
    QcGamepadButton pickBlock = QcGamepadButton::West;
    QcGamepadButton hotbarRadial = QcGamepadButton::DpadLeft;

    // The button of `action`; nullopt for the triggers.
    std::optional<QcGamepadButton> button(Action action) const;
};

// Holding the drop button this long throws the rest of the stack.
inline constexpr qint64 dropStackMs = 500;

// A trigger counts as pressed above this (after dead zone and curve) and as released again below
// releaseThreshold, so a trigger resting near the point does not flicker.
inline constexpr float pressThreshold = 0.5f;
inline constexpr float releaseThreshold = 0.3f;
// The walking stick must point at least this far forward to start or keep a sprint.
inline constexpr float sprintForward = 0.7f;
// A stick pushed further than this (raw, before the dead zone) is meant: it takes the lead from
// another pad, a drifting stick does not.
inline constexpr float leadDeflection = 0.5f;

// Forward (positive) and strafe right (positive) from the shaped left stick (y positive down).
struct Walk
{
    float forward = 0.0f;
    float strafe = 0.0f;

    bool moving() const { return forward != 0.0f || strafe != 0.0f; }
    bool operator==(const Walk &other) const = default;
};
Walk walk(const QcGamepadState &shaped);

// The turn of one frame in degrees (x yaw to the right, y like a mouse moving down) from the shaped
// right stick: full deflection turns `lookSpeed` degrees per second. QcMouseLook applies it with
// sensitivity 1 and the invert setting.
QPointF lookDelta(const QcGamepadState &shaped, double lookSpeed, double seconds);

// Whether a stick of the raw state is pushed beyond leadDeflection.
bool clearlyDeflected(const QcGamepadState &raw);
// Whether a stick of the shaped state is outside its dead zone (the pad walks or looks).
bool deflected(const QcGamepadState &shaped);

// Whether the shaped trigger `value` holds the button, given whether it held it before.
bool triggerHeld(float value, bool wasHeld);

// Whether `action` is held in `shaped` (triggers with the hysteresis above, from `wasHeld`).
bool held(Action action, const QcGamepadState &shaped, const Bindings &bindings, bool wasHeld);

// The hotbar slot `steps` from `slot`, wrapping around the `size` slots.
int hotbarStep(int slot, int steps, int size);

} // namespace QcGamepadMapping
