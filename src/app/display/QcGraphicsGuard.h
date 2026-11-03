// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "app/controllers/QcScreenState.h"
#include "clientcore/settings/QcDeviceLossPolicy.h"
#include "clientcore/settings/QcRenderOptions.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QTimer>

#include <atomic>
#include <functional>
#include <memory>

class QcRenderSettings;
class QcWorldPause;
class QcWorldView;

// Keeps the world view's frames within what a weak GPU can do (decision E-963). It hands the
// render options to the view: the player's, but the classic look while QcDeviceLossPolicy keeps
// the realistic mode off after a lost graphics device (a cool-down, or the rest of the session
// after a second loss, with a notice). And it keeps the view still behind a paused pause menu and
// behind full-screen screens: the picture drawn last stays instead of drawing the world every frame.
class QcGraphicsGuard : public QObject
{
    Q_OBJECT

public:
    QcGraphicsGuard(QcRenderSettings &settings, QcScreenState &screens, QcWorldPause &pause, QObject *parent = nullptr);

    // The view that draws the world (null when there is none); gets the options and the still state.
    void setView(QcWorldView *view);
    // The window whose lost devices are watched (its scene graph invalidated and initialised again).
    void setWindow(QQuickWindow *window);
    // Whether the view stays still now (a paused pause menu or a full-screen screen in a world).
    [[nodiscard]] bool still() const;
    // Whether a view stays still in the phase `playing` (a world is shown) under `overlay` while the
    // server pauses (`worldPaused`) or not: behind a full-screen screen always, behind the pause
    // menu and its screens only while the world pauses (a world others play in runs on behind it).
    [[nodiscard]] static bool stillFor(bool playing, QcScreenState::Overlay overlay, bool worldPaused);
    // The options the view draws with.
    [[nodiscard]] QcRenderOptions drawnOptions() const;
    // GUI thread: the window's graphics device was lost and made anew.
    void deviceLost();

    // Test hooks: the classic frames after a first loss (a new policy, losses before forgotten)
    // and the policy's clock in milliseconds (default: since the guard was made).
    void setCoolDownMs(qint64 ms);
    void setClock(std::function<qint64()> clock);

private:
    // Connects the render thread's scene graph signals of `window` (the device's loss and return).
    void watchDevice(QQuickWindow &window);
    // Render thread: has `guard` hear of the lost device on the GUI thread, if it still lives then.
    static void postDeviceLost(const QPointer<QcGraphicsGuard> &guard);
    // Hands drawnOptions() to the view.
    void apply();
    void updateStill();
    // The player changed the render options: a new choice of the mode forgets the losses.
    void onSettingsChanged();
    // The cool-down timer ran out: realistic again, or the timer again for what is left (a timer
    // may fire a little early).
    void onCoolDownEnd();
    // The notice of the fall-back for the session.
    static QString fallBackNotice();
    [[nodiscard]] QcWorldView *view() const { return m_view; }
    [[nodiscard]] QcDeviceLossPolicy &policy() { return m_policy; }
    [[nodiscard]] const QcDeviceLossPolicy &policy() const { return m_policy; }
    // The policy's clock in milliseconds.
    [[nodiscard]] qint64 now() const { return m_clock ? m_clock() : m_elapsed.elapsed(); }
    [[nodiscard]] QQuickWindow *window() const { return m_window; }

    QcRenderSettings &m_settings;
    QcScreenState &m_screens;
    QcWorldPause &m_pause;
    QPointer<QcWorldView> m_view;
    QPointer<QQuickWindow> m_window;
    QcDeviceLossPolicy m_policy;
    // The render mode the player chose last.
    QcRenderOptions::Mode m_chosenMode;
    QElapsedTimer m_elapsed;
    // Test hook setClock(); empty: m_elapsed.
    std::function<qint64()> m_clock;
    // Ends the cool-down: the realistic mode is drawn again.
    QTimer m_coolDownEnd; // NOLINT(qc-member-access): a timer, started and stopped through its own API
    // Set on the render thread when the scene graph is invalidated, taken when it is initialised again.
    std::shared_ptr<std::atomic_bool> m_invalidated = std::make_shared<std::atomic_bool>(false);
};
