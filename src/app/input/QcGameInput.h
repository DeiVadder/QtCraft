#pragma once

#include "app/input/QcRelativeMouse.h"
#include "clientcore/camera/QcMouseCapture.h"
#include "clientcore/input/QcPointerRadial.h"
#include "clientcore/player/QcFlightLatch.h"
#include "clientcore/player/QcSprintLatch.h"
#include "common/gameplay/QcKeyBindings.h"

#include <QObject>
#include <QPointF>
#include <QSet>
#include <QtQmlIntegration/qqmlintegration.h>

class QcAudioEngine;
class QcHandbookController;
class QcScreenState;
class QcSettingsController;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;
struct QcSessionParts;

// Keyboard and mouse of the world (GameController.input): world keys, keys held for moving and
// looking, mouse capture and mouse look, mining and using with the buttons, the hotbar wheel, and
// the click sound of UI buttons. Filters the application's events. Follows the screen state's
// signals (held keys forgotten, mouse freed, capture synced) and opens screens through it.
class QcGameInput : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool mouseCaptured READ mouseCaptured NOTIFY mouseCapturedChanged FINAL)
    // The player list key is held down (decision E-259); --overlay players holds it for screenshots.
    Q_PROPERTY(bool playerListHeld READ playerListHeld NOTIFY playerListHeldChanged FINAL)
    // The hotbar's radial menu held open by its key, chosen with the mouse (decision E-614).
    Q_PROPERTY(bool radialOpen READ radialOpen NOTIFY radialChanged FINAL)
    Q_PROPERTY(int radialSlot READ radialSlot NOTIFY radialChanged FINAL)

public:
    static constexpr float keyboardLookSpeed = 120.0f;

    // The screens the world keys open; all of them outlive this object.
    struct Screens
    {
        QcScreenState &state;
        QcHandbookController &handbook;
    };

    QcGameInput(Screens screens, const QcSessionParts &parts, QcSettingsController &settings, QcAudioEngine *audio,
                QObject *parent = nullptr);
    ~QcGameInput() override;

    bool mouseCaptured() const { return m_capture.captured(); }
    // Frees the mouse (window lost the focus, world closed); a click or the next step back from a
    // screen into the world captures it again.
    void releaseMouse();
    // Called after every change of the screens, the phase or the player's life: captures the mouse
    // when the world takes the input back and frees it when a screen opens (QcMouseCapture).
    void syncCapture();
    // Scripted run (--background): the mouse is never captured, input comes only from the script.
    void setBackgroundRun(bool background) { m_backgroundRun = background; }
    // A scripted run's virtual pad steers without the window's focus (it never gets it there).
    void setScriptedPad(bool scripted) { m_scriptedPad = scripted; }

    // Forgets the keys held down.
    void clearKeys();
    bool playerListHeld() const { return m_playerListHeld; }
    void setPlayerListHeld(bool held);
    bool radialOpen() const { return m_radial.isOpen(); }
    int radialSlot() const { return m_radial.highlighted(); }
    // Keys held as if pressed (benchmark walk).
    void holdKeys(QSet<int> keys) { m_keysDown = std::move(keys); }
    // Mouse motion since the last frame; taken every frame, so motion from a frame without a world
    // never turns the view later.
    QPointF takeMouseDelta();
    // Turns the view by the mouse motion and applies the held keys to the player's intent.
    void applyFrame(QPointF mouseDelta, float frameSeconds);
    // The world takes the input and its window has the focus: a gamepad may steer (decision E-541;
    // SDL reports pads also while the window is in the background).
    bool worldFocused() const;
    // The world takes keyboard and mouse: playing, no screen open, alive.
    bool worldInput() const;
    // A key action of the world that opens or shows something (pause, inventory, chat, command,
    // handbook, camera view, debug overlay and options, labels, player list), from its key or from
    // a pad (decision E-580); `pressed` false only ends the held player list. False for other actions.
    bool worldAction(QcKeyBindings::Action action, bool pressed);

    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void mouseCapturedChanged();
    void playerListHeldChanged();
    void radialChanged();
    // The handbook key was pressed in the world: the handbook opens at what the crosshair points
    // at, else at the page shown last (the game knows the target from the HUD).
    void handbookRequested();

private:
    bool filterWorldEvent(QEvent *event);
    // World keys while no screen is open; every key event is taken, so nothing else reacts to it.
    void handleWorldKey(const QKeyEvent &key);
    // Keys that act on the player: use, pick block, mine, drop, walking forward (sprint) and
    // jumping (flight); false for other actions.
    bool handlePlayerAction(QcKeyBindings::Action action, const QKeyEvent &key);
    // The targeted block's item into the hand (decision E-285).
    void pickBlock() const;
    void handleKeyRelease(const QKeyEvent &key);
    void handleMousePress(const QMouseEvent &mouse);
    void handleMouseButton(Qt::MouseButton button, bool pressed) const;
    QcMouseCapture::Situation situation() const;
    // Hides the cursor and starts relative motion, or the opposite.
    void applyCapture(QcMouseCapture::Change change);
    bool handleWheel(const QWheelEvent &wheel) const;
    bool handleMouseMove(const QMouseEvent &mouse);
    // Mouse motion of the world: turns the view, or points into the open radial menu.
    void turn(QPointF delta);
    // Closes the radial menu, selecting its slot when `select`.
    void closeRadial(bool select);
    bool keyDown(QcKeyBindings::Action action) const;
    void updateIntentFromKeys(float frameSeconds);
    // Plays the click sound when the mouse is released over a button or an inventory slot.
    void clickIfButton(QPointF scenePosition) const;

    Screens m_screens;
    const QcSessionParts &m_parts;
    QcSettingsController &m_settings;
    QcAudioEngine *m_audio = nullptr;
    QcRelativeMouse m_relativeMouse;
    QSet<int> m_keysDown;
    QcMouseCapture m_capture;
    // Sprint by a press of the sprint key or a double tap of forward (decision E-280).
    QcSprintLatch m_sprint;
    // Flying by a double tap of jump in creative mode (decision E-284).
    QcFlightLatch m_flight;
    bool m_backgroundRun = false;
    bool m_scriptedPad = false;
    bool m_playerListHeld = false;
    QcPointerRadial m_radial;
};
