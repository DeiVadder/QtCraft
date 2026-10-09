#pragma once

#include "QcKeyBindings.h"
#include "QcRelativeMouse.h"

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

public:
    static constexpr float keyboardLookSpeed = 120.0f;

    QcGameInput(QcGameController &game, const QcSessionParts &parts, QcSettingsController &settings, QcAudioEngine *audio,
                QObject *parent = nullptr);

    bool mouseCaptured() const { return m_mouseCaptured; }
    Q_INVOKABLE void captureMouse();
    Q_INVOKABLE void releaseMouse();
    // Scripted run (--background): the mouse is never captured, input comes only from the script.
    void setBackgroundRun(bool background) { m_backgroundRun = background; }

    // Forgets the keys held down.
    void clearKeys() { m_keysDown.clear(); }
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

private:
    // The world takes keyboard and mouse: playing, no screen open, alive.
    bool worldInput() const;
    bool filterWorldEvent(QEvent *event);
    // World keys while no screen is open; every key event is taken, so nothing else reacts to it.
    void handleWorldKey(const QKeyEvent &key);
    void handleKeyRelease(const QKeyEvent &key);
    void handleMousePress(const QMouseEvent &mouse);
    void handleMouseButton(Qt::MouseButton button, bool pressed) const;
    bool handleWheel(const QWheelEvent &wheel) const;
    bool handleMouseMove(const QMouseEvent &mouse) const;
    bool keyDown(QcKeyBindings::Action action) const;
    void updateIntentFromKeys(float frameSeconds) const;
    // Plays the click sound when the mouse is released over a button of the UI.
    void clickIfButton(QPointF scenePosition) const;

    QcGameController &m_game;
    const QcSessionParts &m_parts;
    QcSettingsController &m_settings;
    QcAudioEngine *m_audio = nullptr;
    QcRelativeMouse m_relativeMouse;
    QSet<int> m_keysDown;
    bool m_mouseCaptured = false;
    bool m_backgroundRun = false;
};
