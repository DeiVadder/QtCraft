// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/display/QcGraphicsGuard.h"

#include "app/controllers/QcScreenState.h"
#include "app/controllers/QcWorldPause.h"
#include "app/debug/QcAppLogging.h"
#include "app/rendering/QcWorldView.h"
#include "app/settings/QcRenderSettings.h"

#include <QCoreApplication>

QcGraphicsGuard::QcGraphicsGuard(QcRenderSettings &settings, QcScreenState &screens, QcWorldPause &pause, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_screens(screens)
    , m_pause(pause)
    , m_chosenMode(settings.options().mode)
{
    m_elapsed.start();
    m_coolDownEnd.setSingleShot(true);
    // A coarse timer may fire about 5 % early; onCoolDownEnd() checks the clock anyway.
    m_coolDownEnd.setTimerType(Qt::PreciseTimer);
    connect(&m_coolDownEnd, &QTimer::timeout, this, &QcGraphicsGuard::onCoolDownEnd);
    connect(&m_settings, &QcRenderSettings::changed, this, &QcGraphicsGuard::onSettingsChanged);
    connect(&m_screens, &QcScreenState::overlayChanged, this, &QcGraphicsGuard::updateStill);
    connect(&m_screens, &QcScreenState::phaseChanged, this, &QcGraphicsGuard::updateStill);
    connect(&m_pause, &QcWorldPause::changed, this, &QcGraphicsGuard::updateStill);
}

void QcGraphicsGuard::setView(QcWorldView *view)
{
    m_view = view;
    apply();
    updateStill();
}

void QcGraphicsGuard::setWindow(QQuickWindow *window)
{
    if (this->window() == window) {
        return;
    }
    if (this->window()) {
        disconnect(this->window(), nullptr, this, nullptr);
    }
    m_window = window;
    if (!window) {
        return;
    }

    watchDevice(*window);
}

void QcGraphicsGuard::watchDevice(QQuickWindow &window)
{
    // Both on the render thread. While the window lives its scene graph is invalidated and
    // initialised again only when the graphics device was lost or reset (Qt Quick keeps it
    // otherwise). The lambdas touch nothing of the guard: the guard as their context disconnects
    // them when it goes, but an emission already under way on the render thread may still run, so
    // they keep the flag shared and post a weak pointer that is resolved on the GUI thread
    // (postDeviceLost).
    const std::shared_ptr<std::atomic_bool> invalidated = m_invalidated;
    const QPointer<QcGraphicsGuard> guard(this);
    connect(&window, &QQuickWindow::sceneGraphInvalidated, this, [invalidated] { invalidated->store(true); }, Qt::DirectConnection);
    connect(
        &window, &QQuickWindow::sceneGraphInitialized, this,
        [guard, invalidated] {
            if (invalidated->exchange(false)) {
                postDeviceLost(guard);
            }
        },
        Qt::DirectConnection);
}

void QcGraphicsGuard::postDeviceLost(const QPointer<QcGraphicsGuard> &guard)
{
    // Copying the weak pointer here is thread-safe (an atomic count); reading it is not, so it is
    // read only on the GUI thread, where the application object lives.
    QCoreApplication *const application = QCoreApplication::instance();
    if (!application) {
        return;
    }
    QMetaObject::invokeMethod(
        application,
        [guard] {
            if (guard) {
                guard->deviceLost();
            }
        },
        Qt::QueuedConnection);
}

bool QcGraphicsGuard::still() const
{
    return stillFor(m_screens.playing(), m_screens.overlay(), m_pause.paused());
}

bool QcGraphicsGuard::stillFor(bool playing, QcScreenState::Overlay overlay, bool worldPaused)
{
    if (!playing) {
        return false;
    }
    return QcScreenState::isFullScreen(overlay) || (QcWorldPause::pausesWorld(overlay) && worldPaused);
}

QcRenderOptions QcGraphicsGuard::drawnOptions() const
{
    QcRenderOptions options = m_settings.options();
    if (options.realistic() && !policy().realisticAllowed(now())) {
        options.mode = QcRenderOptions::Mode::Classic;
    }
    return options;
}

void QcGraphicsGuard::deviceLost()
{
    const bool realistic = view() && view()->renderOptions().realistic();
    const qint64 lostMs = now();
    switch (policy().deviceLost(lostMs, realistic)) {
    case QcDeviceLossPolicy::Response::Recover:
        qCWarning(lcDisplay) << "Graphics: the device was lost; drawing on as before";
        break;
    case QcDeviceLossPolicy::Response::CoolDown:
        qCWarning(lcDisplay) << "Graphics: the device was lost in the realistic mode; drawing the classic look for"
                             << policy().coolDownLeftMs(lostMs) / 1000 << "s";
        m_coolDownEnd.start(static_cast<int>(policy().coolDownLeftMs(lostMs)));
        break;
    case QcDeviceLossPolicy::Response::FallBack:
        qCWarning(lcDisplay) << "Graphics: the device was lost twice in the realistic mode; drawing the classic look for the session";
        m_coolDownEnd.stop();
        m_settings.announce(fallBackNotice());
        break;
    }
    apply();
}

void QcGraphicsGuard::setCoolDownMs(qint64 ms)
{
    policy() = QcDeviceLossPolicy(ms);
    m_coolDownEnd.stop();
    apply();
}

void QcGraphicsGuard::setClock(std::function<qint64()> clock)
{
    m_clock = std::move(clock);
}

void QcGraphicsGuard::onCoolDownEnd()
{
    const qint64 left = policy().coolDownLeftMs(now());
    if (left > 0) {
        m_coolDownEnd.start(static_cast<int>(left));
        return;
    }
    qCInfo(lcDisplay) << "Graphics: the cool-down after the lost device ended, drawing the realistic mode again";
    apply();
}

void QcGraphicsGuard::apply()
{
    if (view()) {
        view()->setRenderOptions(drawnOptions());
    }
}

void QcGraphicsGuard::updateStill()
{
    if (view()) {
        view()->setStill(still());
    }
}

void QcGraphicsGuard::onSettingsChanged()
{
    const QcRenderOptions::Mode mode = m_settings.options().mode;
    if (mode != m_chosenMode) {
        m_chosenMode = mode;
        if (policy().fellBack() || !policy().realisticAllowed(now())) {
            qCInfo(lcDisplay) << "Graphics: the render mode was chosen again; the lost devices before count no more";
        }
        policy().choseAgain();
        m_coolDownEnd.stop();
    }
    apply();
}

QString QcGraphicsGuard::fallBackNotice()
{
    return tr("The graphics driver failed twice in the realistic mode: Classic is drawn for the rest of this session. "
              "Set the render mode to Classic and back to Realistic in the settings to try again.");
}
