#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QtCore/qnamespace.h>

#include <array>
#include <optional>

// Which keys trigger which action of the game, as a table: every action has a primary and a
// secondary key (Qt::Key values, 0 = none). The defaults work on a Mac keyboard without fn: no
// primary key is a function key, because macOS uses F1-F12 for system features unless fn is held;
// the familiar function keys stay as secondary keys. On macOS no default uses Cmd (Qt::Key_Control
// there) or Control (Qt::Key_Meta), whose combinations macOS takes for itself (Cmd+Q, Cmd+Space,
// Control+arrow keys, Control+Space); sprint is Option there and Cmd cannot be bound at all. Key
// names ("V", "F5", "Space", "/") are portable and used in the settings file.
class QcKeyBindings
{
public:
    enum class Action {
        Forward,
        Back,
        Left,
        Right,
        Jump,
        Sneak,
        Sprint,
        LookUp,
        LookDown,
        LookLeft,
        LookRight,
        Mine,
        Use,
        // The targeted block's item into the hand (also the middle mouse button, decision E-285).
        PickBlock,
        Drop,
        Inventory,
        Chat,
        Command,
        CameraView,
        Handbook,
        DebugOverlay,
        // The debug options: visualisations switched one by one (decision E-225).
        DebugOptions,
        Labels,
        // Held down: the list of the players on the server (decision E-259).
        PlayerList,
        Hotbar1,
        Hotbar2,
        Hotbar3,
        Hotbar4,
        Hotbar5,
        Hotbar6,
        Hotbar7,
        Hotbar8,
        Hotbar9,
        // Esc: fixed, because Esc also cancels every screen and the key capture.
        Pause,
    };
    static constexpr int actionCount = int(Action::Pause) + 1;
    static constexpr int slotCount = 2;
    // Mac keyboards (Cmd, Control, Option) or the others (Ctrl, Meta, Alt).
    enum class Platform { Mac, Other };
    // The single key that clears a slot while capturing (and therefore cannot be bound).
    static constexpr int clearKey = Qt::Key_Backspace;

    // A binding moved away from another action to resolve a conflict: `action`'s `slot` now holds
    // `key` (the key the rebound action had before, 0 = none).
    struct Displaced
    {
        Action action = Action::Forward;
        int slot = 0;
        int key = 0;
    };

    static Platform currentPlatform();
    static QcKeyBindings defaults(Platform platform = currentPlatform());
    static const QList<Action> &actions();
    // Stable id used in the settings file and placeholders, e.g. "cameraView", "hotbar1".
    static QString id(Action action);
    static std::optional<Action> actionFromId(const QString &id);
    // The pause key cannot be rebound.
    static bool isFixed(Action action) { return action == Action::Pause; }
    // Keys a player may bind: not Esc, not Backspace (clears), not "unknown", not Cmd on a Mac.
    static bool isBindable(int key, Platform platform);
    // The modifier that makes the drop key throw the whole stack: Ctrl, on a Mac Control (Cmd+Q
    // quits there).
    static Qt::KeyboardModifier stackModifier(Platform platform);
    Platform platform() const { return m_platform; }
    // Portable name of a key; empty for 0 or keys without a name.
    static QString keyName(int key);
    // Inverse of keyName(); std::nullopt for unknown names. "" gives 0 (no key).
    static std::optional<int> keyFromName(const QString &name);
    static bool isFunctionKey(int key);

    int key(Action action, int slot) const;
    // Both keys of an action, primary first, without empty slots.
    QList<int> keys(Action action) const;
    bool matches(Action action, int key) const;
    // The first action (in table order) bound to `key`.
    std::optional<Action> actionFor(int key) const;
    // The action of a key press with these modifiers. On a Mac a press with Cmd (Qt::ControlModifier)
    // triggers nothing, so Cmd+Q, Cmd+H, Cmd+W … keep their system meaning; Option and Control do.
    std::optional<Action> actionForPress(int key, Qt::KeyboardModifiers modifiers) const;
    // Binds `key` (0 clears) to the slot. A key used elsewhere is swapped: the other action gets the
    // key this slot had before, so no key ends up with two actions. Returns that change, if any.
    // Fixed actions and keys that are not bindable are refused (std::nullopt, nothing changes).
    std::optional<Displaced> bind(Action action, int slot, int key);
    // Sets a slot as it is, without resolving conflicts (loading; see dropConflicts()). Fixed
    // actions and unbindable keys are ignored.
    void setKey(Action action, int slot, int key);
    // Pairs of (action, slot) that share a key with an earlier one; empty for a consistent table.
    QList<std::pair<Action, int>> conflicts() const;
    // Removes keys used twice. A key stays with the first of the `preferred` slots that holds it
    // (loading: the slots the player set himself beat defaults that came later), otherwise with the
    // first action in table order. Returns the slots that lost their key.
    QList<std::pair<Action, int>> dropConflicts(const QList<std::pair<Action, int>> &preferred = {});

    // Values for handbook placeholders: "key.<id>" lists all keys of an action, "key1.<id>" only
    // the primary key, both as key names; on a Mac the modifiers are "MacCmd", "MacControl" and
    // "MacOption", so the handbook names them like the keyboard.
    // "key.stackModifier" names the modifier of stackModifier().
    QMap<QString, QStringList> placeholderKeys() const;

    friend bool operator==(const QcKeyBindings &, const QcKeyBindings &) = default;

private:
    void set(Action action, int slot, int key) { m_keys[std::size_t(action)][std::size_t(slot)] = key; }

    std::array<std::array<int, slotCount>, actionCount> m_keys{};
    Platform m_platform = Platform::Other;
};
