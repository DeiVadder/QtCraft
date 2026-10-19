#pragma once

#include "clientcore/input/QcGamepadState.h"

#include <QList>
#include <QPointF>
#include <QString>
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
    // Photo mode (decision E-692), as with their keys; without a button by default.
    HideHud,
    Screenshot,
    Count
};
inline constexpr std::size_t actionCount = std::size_t(Action::Count);
// The "button" of an action that has none (decision E-692): every button of a pad carries an
// action already, so the photo mode's actions start without one until the player gives them one.
inline constexpr QcGamepadButton unbound = QcGamepadButton::Count;

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
    QcGamepadButton hideHud = unbound;
    QcGamepadButton screenshot = unbound;

    // The button of `action`; nullopt for the triggers and actions without a button.
    std::optional<QcGamepadButton> button(Action action) const;
    // The action on `button`, nullopt for a free one (the first in table order for a table in conflict).
    std::optional<Action> action(QcGamepadButton button) const;
    // Sets the button of a rebindable action as it is (`unbound` too), without resolving conflicts
    // (loading).
    void setButton(Action action, QcGamepadButton button);
    // Binds `button` to `action` (decision E-611). An action that had the button already gets the
    // action's old one (a swap, which leaves it without a button when the action had none), so
    // every button keeps at most one action; that action is returned.
    // Refused (nullopt, nothing changes) for the triggers, buttons that cannot be bound and a swap
    // that would leave an action bound by default without a button (`stranded`).
    std::optional<Action> bind(Action action, QcGamepadButton button);
    // The action bound by default (pause, jump, ...) that binding `button` to `action` would leave
    // without a button, because `action` has none to give in the swap; nullopt when the bind is fine.
    std::optional<Action> stranded(Action action, QcGamepadButton button) const;
    // The actions sharing a button with an earlier one in table order; empty for a usable table.
    QList<Action> conflicts() const;

    bool operator==(const Bindings &other) const = default;
};

// A button combination (decision E-692): `button` pressed while the button of `modifier` is held
// runs `action` instead of the button's own action, for that whole press. The table is fixed (the
// controller page shows it read-only); the modifier follows the binding of its action.
struct Combo
{
    Action action;
    Action modifier;
    QcGamepadButton button;
};
// The player list's button (Back) plus Y saves a screenshot, plus X hides the HUD; the player list
// shows while Back is held, as without the combination.
inline constexpr std::array<Combo, 2> combos{{
    {Action::Screenshot, Action::PlayerList, QcGamepadButton::North},
    {Action::HideHud, Action::PlayerList, QcGamepadButton::West},
}};
// The combination of `action`; nullopt for an action without one.
std::optional<Combo> comboOf(Action action);
// The held actions `now` (from held(), `before` the previous state) with the combos applied: a
// combo's button pressed while its modifier is held holds the combo's action and not the button's
// own; a press that began before the modifier stays the button's own. A combo whose modifier has no
// button or the combo's button itself does nothing.
[[nodiscard]] std::array<bool, actionCount> withCombos(std::array<bool, actionCount> now, const std::array<bool, actionCount> &before,
                                                     const QcGamepadState &shaped, const Bindings &bindings);

// The id of an action in settings files and in QML ("jump", "hotbarRadial", ...).
QString actionName(Action action);
std::optional<Action> actionFromName(const QString &name);
// Whether the player may give `action` another button: all but the triggers' actions.
bool rebindable(Action action);
// Whether a button may carry an action: all but Guide, which the system often takes itself.
bool bindable(QcGamepadButton button);

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
// Whether a raw state shows a deliberate use: a button, a trigger beyond pressThreshold or a
// clearly pushed stick (the pad becomes the input in use, decisions E-583 and E-613).
bool used(const QcGamepadState &raw);
// Whether a stick of the shaped state is outside its dead zone (the pad walks or looks).
bool deflected(const QcGamepadState &shaped);

// Whether the shaped trigger `value` holds the button, given whether it held it before: pressed
// above `press`, released again below releaseFor(press) (decision E-610).
bool triggerHeld(float value, bool wasHeld, float press = pressThreshold);
// The release point of a trigger pressed at `press`: 0.2 lower, at least 0.05.
float releaseFor(float press);

// Whether `action` is held in `shaped` (triggers with the hysteresis above, from `wasHeld`).
bool held(Action action, const QcGamepadState &shaped, const Bindings &bindings, bool wasHeld, float press = pressThreshold);

// The hotbar slot `steps` from `slot`, wrapping around the `size` slots.
int hotbarStep(int slot, int steps, int size);

} // namespace QcGamepadMapping
