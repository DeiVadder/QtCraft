// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/input/QcGamepadMenuInput.h"
#include "clientcore/input/QcSpatialNavigation.h"

#include <QElapsedTimer>
#include <QEvent>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>


// Carries out the pad's commands on the screens of a window (decision E-583): moves the focus to
// the neighbour in a direction (QcSpatialNavigation over QcFocusCandidates), scrolls it into view
// and hovers it; confirms with a left click on the focused item (the on-screen keyboard for a text
// field), secondary with a right click, tertiary with a shift-click, goes back with Escape, and
// switches the tab of the nearest TabBar; left and right change a horizontal slider or a spin box
// (as their arrow keys do) instead of leaving it. Clicks and keys go in through the platform's input path
// (QWindowSystemInterface), so every screen handles them as it handles the mouse and keyboard. An
// item of the scope may take a command first with a QML function `gamepadCommand(name)` returning
// true (names: up, down, left, right, confirm, back, secondary, tertiary, start, tabPrevious,
// tabNext); the nearest one above the focus is asked. Start does what back does unless taken.
// An item with the QML property `padLeftOnly` set (a side bar's back entry) is never where the focus
// starts; it is reached by moving left, or up from within its own group (the first category below
// it), never down (docs/UI-Leitfaden.md 10, decision E-925).
// The scope (QcFocusCandidates::scope, a walk over the window's items) is kept between calls: each
// command looks it up again, and so does focusItem() after invalidateScope() (the focus changed, or
// the owner's regular refresh).
class QcMenuNavigator : public QObject
{
    Q_OBJECT

public:
    using Command = QcGamepadMenuInput::Command;

    explicit QcMenuNavigator(QObject *parent = nullptr);
    ~QcMenuNavigator() override;
    Q_DISABLE_COPY_MOVE(QcMenuNavigator)

    void setWindow(QQuickWindow *window);
    QQuickWindow *window() const { return m_window; }

    void run(Command command);
    // Scrolls the list or page around the focus (else the scope's first one) by `pixels`; a focus
    // in that list that is no longer shown whole moves to the nearest row that is.
    void scroll(float pixels);
    // The same sideways (the right stick's x, decision E-924): positive pixels to the right. A list
    // that snaps to its rows stays as it is; pages without room to the sides as well.
    void scrollSideways(float pixels);
    // The next scope lookup walks the window's items again.
    void invalidateScope() { m_scopeValid = false; }

    // The focused item when it is a candidate of the current scope, else nullptr.
    QQuickItem *focusItem() const;
    // Without a focused candidate, focuses where the focus starts: the one focused last in this
    // scope, else a highlighted (default) button, else the top left (never a `padLeftOnly` item).
    void focusStart();
    // Moves the pointer over the focused candidate, so the hover follows a focus that QML moved.
    void hoverFocus();
    // Input made by the navigator is being sent (it is not the mouse's or the keyboard's).
    bool sending() const { return m_sending; }

    static QString commandName(Command command);

signals:
    // Confirm on an editable text field: the on-screen keyboard should open for it.
    void keyboardRequested(QQuickItem *field);
    // The navigator moved the focus or acted on it.
    void acted();

private:
    QQuickItem *currentScope() const;
    // Asks the QML functions gamepadCommand() from the focus up to the scope; whether one took it.
    bool offerToScreen(const QQuickItem *scope, QQuickItem *focus, Command command);
    // Without a focus inside the scope, focuses where the focus starts.
    void focusInScope(const QQuickItem *scope);
    void move(const QQuickItem *scope, QQuickItem *current, QcSpatialNavigation::Direction direction);
    // Left or right on a horizontal slider or a spin box: changes its value; whether it was one.
    bool adjust(QQuickItem *item, QcSpatialNavigation::Direction direction);
    // The list or page the right stick scrolls: the one around the focus inside the scope, else the
    // scope's first shown one; nullptr without a scope.
    QQuickItem *scrolledFlickable();
    // A focus in `flickable` that is not shown whole moves to the nearest candidate there that is.
    void keepFocusInView(const QQuickItem *flickable);
    void focus(QQuickItem *item);
    void confirm(QQuickItem *item);
    void switchTab(const QQuickItem *scope, QQuickItem *current, int step);
    void click(QQuickItem *item, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
    void hover(QQuickItem *item);
    void sendMouse(QPointF scenePosition, Qt::MouseButtons buttons, Qt::MouseButton button, QEvent::Type type,
                   Qt::KeyboardModifiers modifiers);
    void sendKey(int key);
    // The time of the next sent event: rises with the wall clock and never repeats.
    ulong nextTimestamp();
    // Scrolls the flickables around `item` so that it is shown whole.
    static void ensureVisible(QQuickItem *item);

    QPointer<QQuickWindow> m_window;
    // The clock of the sent events (see pressSpacingMs) and the last time given out.
    QElapsedTimer m_clock;
    ulong m_timestamp = 0;
    mutable QPointer<QQuickItem> m_scope;
    mutable bool m_scopeValid = false;
    bool m_sending = false;
    // The item focused last in each scope, to come back to (a combo box after its list closed).
    QHash<const QQuickItem *, QPointer<QQuickItem>> m_lastFocus;
};
