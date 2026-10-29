// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/input/QcGamepadRoster.h"
#include "app/input/QcMenuNavigator.h"
#include "clientcore/input/QcGamepadMenuInput.h"

#include <QObject>
#include <QPointer>
#include <QHash>
#include <QRectF>
#include <QSet>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>
#include <optional>

class QcGamepadControls;
class QcGamepadSource;
class QMouseEvent;
class QWindow;

// The pad on the screens, for QML (GameController.gamepad, decisions E-581 to E-584): which input
// was used last (`active`: the pad; a key, a click, the wheel or a moved mouse give it back), the
// focus ring around the focused item while the pad leads on a screen, the hotbar's radial menu of
// the world (QcGamepadControls) and the on-screen keyboard's requests. Screens take the pad's
// commands (QcGamepadMenuInput) while the world does not take the input (the gate) and the window
// has the focus; QcMenuNavigator carries them out. Lives in the GUI thread.
class QcGamepadUi : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool active READ active NOTIFY activeChanged FINAL)
    Q_PROPERTY(bool focusShown READ focusShown NOTIFY focusChanged FINAL)
    Q_PROPERTY(QRectF focusRect READ focusRect NOTIFY focusChanged FINAL)
    // Where the ring is drawn: the open popup the focus is in (so it shows over the popup), else
    // the screens (null).
    Q_PROPERTY(QQuickItem *focusLayer READ focusLayer NOTIFY focusChanged FINAL)
    Q_PROPERTY(bool radialOpen READ radialOpen NOTIFY radialChanged FINAL)
    Q_PROPERTY(int radialSlot READ radialSlot NOTIFY radialChanged FINAL)
    // The connected pads, the one in use and its button labels (decisions E-612, E-613).
    Q_PROPERTY(QcGamepadRoster *pads READ pads CONSTANT FINAL)
    // The input of each world action for the leading pad's profile (decision E-611): actions.inventory
    // is "north" unless rebound; mine and use are "rightTrigger" and "leftTrigger".
    Q_PROPERTY(QVariantMap actions READ actions NOTIFY actionsChanged FINAL)
    // The next button pressed on any pad is captured for the settings (captureButton()).
    Q_PROPERTY(bool capturing READ capturing NOTIFY capturingChanged FINAL)

public:
    // Whether the screens may take the pad's commands now (the world does not take the input).
    using Gate = std::function<bool()>;

    QcGamepadUi(QcGamepadControls &world, Gate gate, QObject *parent = nullptr);
    ~QcGamepadUi() override;
    Q_DISABLE_COPY_MOVE(QcGamepadUi)

    void setSource(QcGamepadSource *source);
    void setWindow(QQuickWindow *window);
    // Scripted runs: the screens take the pad without the window's focus (it never gets it there).
    void setIgnoreWindowFocus(bool ignore) { m_ignoreWindowFocus = ignore; }

    bool active() const { return m_active; }
    void setActive(bool active);
    bool focusShown() const { return m_focusShown; }
    QRectF focusRect() const { return m_focusRect; }
    QQuickItem *focusLayer() const { return m_focusLayer; }
    bool radialOpen() const;
    int radialSlot() const;
    QcGamepadRoster *pads() { return &m_roster; }
    QVariantMap actions() const;
    bool capturing() const { return m_capturing; }

    // Waits for a button of any pad (decision E-611): the screens ignore the pad meanwhile, and the
    // next press of a button that can carry an action ends it with buttonCaptured(). Ends without
    // one with cancelCapture() (the settings closing), a key or a click anywhere in the game
    // (taken by the capture alone) or the last pad disconnecting.
    Q_INVOKABLE void captureButton();
    Q_INVOKABLE void cancelCapture();

    QcMenuNavigator &navigator() { return m_navigator; }
    QcGamepadMenuInput &menuInput() { return m_menu; }
    // The focused item for logs: its objectName, else its class and text (empty without focus).
    QString focusDescription() const;
    // Calls of the per-frame hook and the event filter so far (tests): both are attached only while
    // the pad leads, so without a pad in use the game's frames and events never reach them.
    quint64 hookCalls() const { return m_hookCalls; }

    // A screen opened a text field for the pad (the chat line): the keyboard opens while the pad leads.
    Q_INVOKABLE void requestKeyboard(QQuickItem *field);

    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void activeChanged();
    void focusChanged();
    void radialChanged();
    // The on-screen keyboard should open for `field` (Main.qml).
    void keyboardRequested(QQuickItem *field);
    void actionsChanged();
    void capturingChanged();
    // A button was pressed while capturing (its name: "south", "dpadLeft", ...).
    void buttonCaptured(const QString &button);

private:
    bool screensTakeInput() const;
    // Attaches the per-frame hook (`watching`) and the application's event filter (`watching` or
    // capturing) or detaches them.
    void watch(bool watching);
    void onFrame();
    void onCommand(QcGamepadMenuInput::Command command);
    void onPadStateChanged(int id);
    // Follows the focus: the ring's place and, when the pad leads, a focus to start from.
    void updateFocus();
    void onActiveFocusChanged();
    // Whether a mouse move hands the input back (asked while the pad leads): not the game's own return
    // to the window's centre, and at least minimumMouseTravel from where the mouse was last seen.
    bool mouseMoved(const QWindow &window, const QMouseEvent &move);
    // While capturing: the first button pressed since the capture began ends it.
    void captureFrom(int id);
    void onPadConnected(int id);
    void onPadDisconnected(int id);

    QcGamepadControls &m_world;
    Gate m_screensTakeInput;
    QcGamepadRoster m_roster;
    QcGamepadMenuInput m_menu;
    QcMenuNavigator m_navigator;
    QPointer<QcGamepadSource> m_source;
    QPointer<QQuickWindow> m_window;
    bool m_ignoreWindowFocus = false;
    bool m_active = false;
    bool m_focusShown = false;
    QRectF m_focusRect;
    QPointer<QQuickItem> m_focusLayer;
    int m_framesSinceScope = 0;
    QMetaObject::Connection m_frameConnection;
    bool m_filtering = false;
    quint64 m_hookCalls = 0;
    // Where the mouse was last seen (global), to tell a moved mouse from a still one.
    std::optional<QPointF> m_lastMouse;
    bool m_capturing = false;
    // The pads' states when the capture began or at their last change since.
    QHash<int, QcGamepadState> m_captureStates;
    // Pads connected during the capture whose first state is still to come.
    QSet<int> m_captureJoined;
};
