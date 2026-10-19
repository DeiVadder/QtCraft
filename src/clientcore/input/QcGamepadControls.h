#pragma once

#include "clientcore/input/QcGamepadMapping.h"
#include "clientcore/input/QcHotbarRadial.h"
#include "clientcore/player/QcFlightLatch.h"
#include "clientcore/player/QcSprintLatch.h"
#include "clientcore/settings/QcGamepadSettings.h"
#include "common/gameplay/QcKeyBindings.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPointer>

#include <array>

class QcClientSession;
class QcGamepadSource;
struct QcPlayerInput;

// A gamepad steering the player (decision E-541): the left stick walks with stepless speed, the
// right stick looks, the triggers mine (right, also attacks) and use (left), the shoulders step
// through the hotbar, the stick clicks sprint (left) and toggle sneaking (right; held while
// flying, where it descends), south jumps and a double tap of it toggles creative flight. Dead
// zones and curve come from the settings (QcGamepadCurve). A second writer of the session's intent
// beside the keyboard and mouse: the pad sets walking, jump, sneak and sprint only while it uses
// them and gives them back once (zero, false) when it stops, so the keyboard's values hold
// otherwise and the device last used wins without the two fighting; looking adds to the mouse.
// Presses are taken from the source's signals with their time, so a click shorter than one poll
// of the source (8 ms with SDL) still counts; they act at the next frame. They are queued only
// while the frames are `active` (world shown, window focused) and at most maxPresses of them: SDL
// reports pads in the background too, and a press there never waits for the window to come back.
// The pad that last pressed something or clearly pushed a stick leads. Driven once per frame
// (QcFrameDriver) or per tick (the control interface); lives in one thread.
// Beyond walking and acting (decision E-580): east drops (held: the stack), west picks the block
// in view, the screens and the player list are asked for with worldAction() like their keys, and
// a held button opens the hotbar's radial menu (QcHotbarRadial), where the right stick chooses a
// slot instead of looking.
class QcGamepadControls : public QObject
{
    Q_OBJECT

public:
    // The most presses waiting for a frame; beyond it the oldest go.
    static constexpr qsizetype maxPresses = 64;

    explicit QcGamepadControls(QObject *parent = nullptr);
    ~QcGamepadControls() override;

    // Damage of this many health points or more vibrates at full strength (decision E-615).
    static constexpr double fullRumbleDamage = 8.0;
    static constexpr int rumbleMs = 220;

    // The pads to follow (nullptr: none); the source must outlive this object or be set to another.
    void setSource(QcGamepadSource *source);
    // The settings of all pads; each pad uses the profile of its model (decision E-611).
    void setSettings(const QcGamepadSettings &settings);
    const QcGamepadSettings &settings() const { return m_settings; }
    // The profile of the leading pad (the standard one without a pad).
    const QcGamepadProfile &profile() const { return m_profile; }
    // The pad that leads (0: none).
    int activePad() const { return m_pad; }
    // The leading pad is the device in use: it pressed a button, a trigger or clearly pushed a stick
    // since it leads and since keyboardOrMouseUsed(). Only then does it vibrate, and only then does
    // its disconnecting open the pause menu (decisions E-613, E-615).
    bool padInUse() const { return m_inUse; }
    // A key, a click or a moved mouse (QcGamepadUi hands the input back): the pad is no longer in use.
    void keyboardOrMouseUsed() { m_inUse = false; }
    // Whether the game's window has the focus (true without a window): the pad does not vibrate in
    // the background.
    void setWindowActive(bool active) { m_windowActive = active; }

    // Presses waiting for the next frame.
    qsizetype pendingPresses() const { return m_presses.size(); }
    // Whether a click of the sneak button toggled sneaking on.
    bool sneakToggled() const { return m_sneakToggled; }
    // The radial menu of the hotbar is held open, and its highlighted slot (-1 while closed).
    bool radialOpen() const { return m_radial.isOpen(); }
    int radialSlot() const { return m_radial.highlighted(); }

    // One frame of `seconds`: the presses since the last frame act and the sticks walk and look.
    // While not `active`, presses are dropped; the first inactive frame gives back what the pad
    // holds (mining, a drawn bow, walking, jump, sneak, sprint). A toggled sneak stays on across
    // inactive frames (screens, focus) and ends with the player's death.
    void applyFrame(QcClientSession &session, float seconds, bool active);
    // The frames stop or turn inactive without one being drawn (the window lost the focus or was
    // minimised): gives back what the pad holds and queues nothing until the next active frame.
    void suspend(QcClientSession *session);
    // Forgets everything without a session (the session ended or changed), a toggled sneak too.
    void reset();

signals:
    // A key action of the world was pressed (or, for the held player list, released): Pause,
    // Inventory, Chat, Handbook, CameraView or PlayerList. The game opens the screen as with the key.
    // The leading pad disconnecting in the world asks for Pause too (decision E-613).
    void worldAction(QcKeyBindings::Action action, bool pressed);
    // Another pad leads (or none), or the settings changed: profile() may differ.
    void profileChanged();
    // The radial menu opened, closed or highlights another slot.
    void radialChanged();

private:
    using Action = QcGamepadMapping::Action;
    using Held = std::array<bool, QcGamepadMapping::actionCount>;

    struct Press
    {
        Action action = Action::Jump;
        bool pressed = false;
        qint64 timeMs = 0;
    };

    // Turns raw states into presses and releases of the leading pad.
    void onStateChanged(int id);
    void onPadConnected(int id);
    void onPadDisconnected(int id);
    // Pad `id` leads (0: none), with the profile of its model.
    void setLead(int id);
    const QcGamepadProfile &profileOf(int id) const;
    // Looks up the profile of every connected pad once (on connecting and with new settings), so
    // a state change finds it without asking the source for the pad's model.
    void refreshProfiles();
    // Vibrates the leading pad when the player lost health since the last frame (decision E-615):
    // only once the server told the health (the vitals start full before that), while the pad is
    // in use and the window has the focus.
    void rumbleWhenHurt(const QcClientSession &session);
    // Whether pad `id`, which does not lead, takes the lead with this state change: a press of a
    // button or trigger does, a clearly pushed stick only while the leading pad's sticks rest.
    bool takesLead(int id, Held before, Held now) const;
    // Queues releases of everything `id` holds (the pad stops leading); what it holds is kept, so
    // its next state change finds no press that is none.
    void releaseHeld(int id);
    // Queues a press or release, while the frames are active.
    void queue(Action action, bool pressed, qint64 timeMs);
    QcGamepadState shapedState(int id) const;
    // Runs the queued presses; returns whether the sprint button was pressed among them.
    bool runPresses(QcClientSession &session);
    void runPress(QcClientSession &session, Press press);
    // Pause, inventory, chat, handbook, camera view and player list; false for other actions.
    bool runWorldPress(Press press);
    // Drop, pick block and the radial menu; false for other actions.
    bool runItemPress(QcClientSession &session, Press press);
    // A drop button held for dropStackMs throws the rest of the stack.
    void dropStackWhenHeld(QcClientSession &session);
    // Closes the radial menu; selects its slot when `select`.
    void closeRadial(QcClientSession *session, bool select);
    // `intent` with walking, jumping, sneaking and sprinting from the pad (it keeps the latches).
    [[nodiscard]] QcPlayerInput walked(QcPlayerInput intent, const QcGamepadState &shaped, bool sprintPressed, bool flying);
    // Gives back the intent fields and buttons the pad holds, then stop().
    void release(QcClientSession &session);
    // Drops the queued presses and the latches and stops queueing; the sneak toggle stays.
    void stop();

    // A flag of the intent the pad shares with the keyboard: the pad sets it while it wants it and
    // gives it back (false) once when it stops; a flag the pad did not set stays as it is.
    class SharedFlag
    {
    public:
        // The flag's new value from its current one and whether the pad wants it.
        [[nodiscard]] bool merged(bool current, bool want);

    private:
        bool m_mine = false;
    };

    QPointer<QcGamepadSource> m_source;
    QcGamepadSettings m_settings;
    QcGamepadProfile m_profile;
    // The profile of each connected pad (into m_settings, refreshed with it).
    QHash<int, const QcGamepadProfile *> m_profiles;
    QElapsedTimer m_clock;
    int m_pad = 0;
    // What each pad held at its last state change (triggers with their hysteresis); follows the
    // pads also while the pad does not lead or the frames are inactive.
    QHash<int, Held> m_held;
    QList<Press> m_presses;
    QcSprintLatch m_sprint;
    QcFlightLatch m_flight;
    // The last frame was active: presses are queued.
    bool m_active = false;
    bool m_inUse = false;
    bool m_windowActive = true;
    bool m_sneakToggled = false;
    // What the pad set in the intent and on the session, to give it back once.
    bool m_walking = false;
    SharedFlag m_jumping;
    SharedFlag m_sneaking;
    SharedFlag m_sprinting;
    bool m_mining = false;
    bool m_using = false;
    bool m_playerList = false;
    QcHotbarRadial m_radial;
    // The radial menu was open in the last active frame; after it closed the right stick looks
    // again only once it was back in its dead zone (it was pointing at a slot).
    bool m_radialWasOpen = false;
    bool m_lookWaitsForNeutral = false;
    // When the drop button went down (-1: not held) and whether its hold threw the stack.
    qint64 m_dropPressedMs = -1;
    // The player's health in the last frame (negative: none seen yet).
    double m_lastHealth = -1.0;
};
