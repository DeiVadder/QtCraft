#pragma once

#include "clientcore/input/QcGamepadMapping.h"
#include "clientcore/player/QcFlightLatch.h"
#include "clientcore/player/QcSprintLatch.h"
#include "clientcore/settings/QcGamepadSettings.h"

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
class QcGamepadControls : public QObject
{
    Q_OBJECT

public:
    // The most presses waiting for a frame; beyond it the oldest go.
    static constexpr qsizetype maxPresses = 64;

    explicit QcGamepadControls(QObject *parent = nullptr);
    ~QcGamepadControls() override;

    // The pads to follow (nullptr: none); the source must outlive this object or be set to another.
    void setSource(QcGamepadSource *source);
    void setSettings(const QcGamepadSettings &settings);
    const QcGamepadSettings &settings() const { return m_settings; }
    // The pad that leads (0: none).
    int activePad() const { return m_pad; }

    // Presses waiting for the next frame.
    qsizetype pendingPresses() const { return m_presses.size(); }
    // Whether a click of the sneak button toggled sneaking on.
    bool sneakToggled() const { return m_sneakToggled; }

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
    void applyWalk(QcPlayerInput &intent, const QcGamepadState &shaped, bool sprintPressed, bool flying);
    // Gives back the intent fields and buttons the pad holds, then stop().
    void release(QcClientSession &session);
    // Drops the queued presses and the latches and stops queueing; the sneak toggle stays.
    void stop();

    QPointer<QcGamepadSource> m_source;
    QcGamepadSettings m_settings;
    QcGamepadMapping::Bindings m_bindings;
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
    bool m_sneakToggled = false;
    // What the pad set in the intent and on the session, to give it back once.
    bool m_walking = false;
    bool m_jumping = false;
    bool m_sneaking = false;
    bool m_sprinting = false;
    bool m_mining = false;
    bool m_using = false;
};
