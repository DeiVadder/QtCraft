#pragma once

#include "app/input/QcRelativeMouse.h"
#include "clientcore/camera/QcMouseCapture.h"
#include "clientcore/session/QcFlightLatch.h"
#include "clientcore/session/QcSprintLatch.h"
#include "common/gameplay/QcKeyBindings.h"

#include <QObject>
#include <QPointF>
#include <QSet>
#include <QtQmlIntegration/qqmlintegration.h>

class QcAudioEngine;
class QcGameController;
class QcSettingsController;
class QKeyEvent;
class QMouseEvent;
class QWheelEvent;
struct QcSessionParts;

// Keyboard and mouse of the world (GameController.input): world keys, keys held for moving and
// looking, mouse capture and mouse look, mining and using with the buttons, the hotbar wheel, and
// the click sound of UI buttons. Filters the application's events.
class QcGameInput : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool mouseCaptured READ mouseCaptured NOTIFY mouseCapturedChanged FINAL)
    // The player list key is held down (decision E-259); --overlay players holds it for screenshots.
    Q_PROPERTY(bool playerListHeld READ playerListHeld NOTIFY playerListHeldChanged FINAL)

public:
    static constexpr float keyboardLookSpeed = 120.0f;

    QcGameInput(QcGameController &game, const QcSessionParts &parts, QcSettingsController &settings, QcAudioEngine *audio,
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

    // Forgets the keys held down.
    void clearKeys();
    bool playerListHeld() const { return m_playerListHeld; }
    void setPlayerListHeld(bool held);
    // Keys held as if pressed (benchmark walk).
    void holdKeys(QSet<int> keys) { m_keysDown = std::move(keys); }
    // Mouse motion since the last frame; taken every frame, so motion from a frame without a world
    // never turns the view later.
    QPointF takeMouseDelta();
    // Turns the view by the mouse motion and applies the held keys to the player's intent.
    void applyFrame(QPointF mouseDelta, float frameSeconds);

    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void mouseCapturedChanged();
    void playerListHeldChanged();

private:
    // The world takes keyboard and mouse: playing, no screen open, alive.
    bool worldInput() const;
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
    bool handleMouseMove(const QMouseEvent &mouse) const;
    bool keyDown(QcKeyBindings::Action action) const;
    void updateIntentFromKeys(float frameSeconds);
    // Plays the click sound when the mouse is released over a button or an inventory slot.
    void clickIfButton(QPointF scenePosition) const;

    QcGameController &m_game;
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
    bool m_playerListHeld = false;
};
