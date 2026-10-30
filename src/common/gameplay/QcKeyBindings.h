// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QtCore/qnamespace.h>

#include <array>
#include <optional>

/*! \brief Which keys trigger which action of the game, as a table: every action has a primary and a
    secondary key (Qt::Key values, 0 = none).

    The defaults work on a Mac keyboard without fn: no primary key is a function key, because
    macOS uses F1-F12 for system features unless fn is held; the familiar function keys stay as
    secondary keys. On macOS no default uses Cmd (Qt::Key_Control there) or Control
    (Qt::Key_Meta), whose combinations macOS takes for itself (Cmd+Q, Cmd+Space, Control+arrow
    keys, Control+Space); sprint is Option there and Cmd cannot be bound at all. Key names ("V",
    "F5", "Space", "/") are portable and used in the settings file. */
class QcKeyBindings
{
public:
    /*! The actions that keys trigger, in table order. */
    enum class Action {
        /*! Walks forward. */
        Forward,
        /*! Walks back. */
        Back,
        /*! Walks left. */
        Left,
        /*! Walks right. */
        Right,
        /*! Jumps (swims up, climbs, flies up). */
        Jump,
        /*! Sneaks (flies down). */
        Sneak,
        /*! Sprints. */
        Sprint,
        /*! Turns the view up. */
        LookUp,
        /*! Turns the view down. */
        LookDown,
        /*! Turns the view left. */
        LookLeft,
        /*! Turns the view right. */
        LookRight,
        /*! Mines the targeted block or hits the targeted creature. */
        Mine,
        /*! Uses the held item or the targeted block. */
        Use,
        /*! The targeted block's item into the hand (also the middle mouse button, decision
            E-285). */
        PickBlock,
        /*! Drops the held item (the whole stack with stackModifier()). */
        Drop,
        /*! Opens the inventory (the item catalogue in creative mode). */
        Inventory,
        /*! Opens the chat. */
        Chat,
        /*! Opens the chat with "/" typed. */
        Command,
        /*! Cycles the camera: first person, behind, front. */
        CameraView,
        /*! Opens the handbook. */
        Handbook,
        /*! The tree of the adventure guide (decision E-924). */
        Advancements,
        /*! Cycles the debug display: off, compact, full (decision E-225). */
        DebugOverlay,
        /*! The debug options: visualisations switched one by one (decision E-225). */
        DebugOptions,
        /*! Shows or hides the labels floating in the world. */
        Labels,
        /*! Held down: the list of the players on the server (decision E-259). */
        PlayerList,
        /*! Selects hotbar slot 1. */
        Hotbar1,
        /*! Selects hotbar slot 2. */
        Hotbar2,
        /*! Selects hotbar slot 3. */
        Hotbar3,
        /*! Selects hotbar slot 4. */
        Hotbar4,
        /*! Selects hotbar slot 5. */
        Hotbar5,
        /*! Selects hotbar slot 6. */
        Hotbar6,
        /*! Selects hotbar slot 7. */
        Hotbar7,
        /*! Selects hotbar slot 8. */
        Hotbar8,
        /*! Selects hotbar slot 9. */
        Hotbar9,
        /*! Held down: the hotbar's radial menu, a slot chosen by moving the mouse (decision
            E-614). */
        HotbarRadial,
        /*! Photo mode (decision E-692): hides the HUD with every overlay and the crosshair (the
            hand too, as the settings say). */
        HideHud,
        /*! Saves a screenshot of the window. */
        Screenshot,
        /*! Fullscreen and back to the window mode before it (decision E-835); F11 as elsewhere. */
        Fullscreen,
        /*! Esc: fixed, because Esc also cancels every screen and the key capture. */
        Pause,
    };
    /*! The number of actions. */
    static constexpr int actionCount = static_cast<int>(Action::Pause) + 1;
    /*! Keys per action: primary and secondary. */
    static constexpr int slotCount = 2;
    /*! \brief Mac keyboards (Cmd, Control, Option) or the others (Ctrl, Meta, Alt). */
    enum class Platform {
        /*! A Mac keyboard. */
        Mac,
        /*! Any other keyboard. */
        Other
    };
    /*! The single key that clears a slot while capturing (and therefore cannot be bound). */
    static constexpr int clearKey = Qt::Key_Backspace;

    /*! \brief A binding moved away from another action to resolve a conflict: \c {action}'s
        \c {slot} now holds \c {key} (the key the rebound action had before, 0 = none). */
    struct Displaced
    {
        /*! The action that got the key. */
        Action action = Action::Forward;
        /*! Its slot. */
        int slot = 0;
        /*! The key; 0 for none. */
        int key = 0;
    };

    /*! Returns the platform of the running program. */
    static Platform currentPlatform();
    /*! Returns the default table for \a platform. */
    static QcKeyBindings defaults(Platform platform = currentPlatform());
    /*! Returns every action in table order. */
    static const QList<Action> &actions();
    /*! Returns the stable id of \a action used in the settings file and placeholders, e.g.
        "cameraView", "hotbar1". */
    static QString id(Action action);
    /*! Returns the action with the id \a id; std::nullopt for unknown ids. */
    [[nodiscard]] static std::optional<Action> actionFromId(const QString &id);
    /*! Returns whether \a action cannot be rebound (the pause key). */
    static bool isFixed(Action action) { return action == Action::Pause; }
    /*! Returns whether a player may bind \a key on \a platform: not Esc, not Backspace (clears),
        not "unknown", not Cmd on a Mac. */
    static bool isBindable(int key, Platform platform);
    /*! Returns the modifier that makes the drop key throw the whole stack on \a platform: Ctrl,
        on a Mac Control (Cmd+Q quits there). */
    static Qt::KeyboardModifier stackModifier(Platform platform);
    /*! Returns the platform the table was made for. */
    Platform platform() const { return m_platform; }
    /*! Returns the portable name of \a key; empty for 0 or keys without a name. */
    static QString keyName(int key);
    /*! Returns the key named \a name, the inverse of keyName(); std::nullopt for unknown names.
        "" gives 0 (no key). */
    [[nodiscard]] static std::optional<int> keyFromName(const QString &name);
    /*! Returns whether \a key is one of the function keys F1 to F35. */
    static bool isFunctionKey(int key);

    /*! Returns the key in \a slot of \a action; 0 for none. */
    int key(Action action, int slot) const;
    /*! Returns both keys of \a action, primary first, without empty slots. */
    QList<int> keys(Action action) const;
    /*! Returns whether \a key is one of the keys of \a action. */
    bool matches(Action action, int key) const;
    /*! Returns the first action (in table order) bound to \a key. */
    [[nodiscard]] std::optional<Action> actionFor(int key) const;
    /*! Returns the action of a press of \a key with \a modifiers. On a Mac a press with Cmd
        (Qt::ControlModifier) triggers nothing, so Cmd+Q, Cmd+H, Cmd+W … keep their system
        meaning; Option and Control do. */
    [[nodiscard]] std::optional<Action> actionForPress(int key, Qt::KeyboardModifiers modifiers) const;
    /*! Binds \a key (0 clears) to \a slot of \a action. A key used elsewhere is swapped: the
        other action gets the key this slot had before, so no key ends up with two actions.
        Returns that change, if any. Fixed actions and keys that are not bindable are refused
        (std::nullopt, nothing changes). */
    std::optional<Displaced> bind(Action action, int slot, int key);
    /*! Sets \a slot of \a action to \a key as it is, without resolving conflicts (loading; see
        dropConflicts()). Fixed actions and unbindable keys are ignored. */
    void setKey(Action action, int slot, int key);
    /*! Pairs of (action, slot) that share a key with an earlier one; empty for a consistent
        table. */
    QList<std::pair<Action, int>> conflicts() const;
    /*! Removes keys used twice. A key stays with the first of the \a preferred slots that holds
        it (loading: the slots the player set himself beat defaults that came later), otherwise
        with the first action in table order. Returns the slots that lost their key. */
    QList<std::pair<Action, int>> dropConflicts(const QList<std::pair<Action, int>> &preferred = {});

    /*! Values for handbook placeholders: "key.<id>" lists all keys of an action, "key1.<id>"
        only the primary key, both as key names; on a Mac the modifiers are "MacCmd",
        "MacControl" and "MacOption", so the handbook names them like the keyboard, and a
        function key that comes first is "fn+F11" (no letter key before it; a Mac needs fn for
        it). "key.stackModifier" names the modifier of stackModifier(). */
    QMap<QString, QStringList> placeholderKeys() const;

    friend bool operator==(const QcKeyBindings &, const QcKeyBindings &) = default;

private:
    void set(Action action, int slot, int key) { m_keys[static_cast<std::size_t>(action)][static_cast<std::size_t>(slot)] = key; }

    std::array<std::array<int, slotCount>, actionCount> m_keys{};
    Platform m_platform = Platform::Other;
};
