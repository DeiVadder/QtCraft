// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/display/QcWindowPlacement.h"

#include "app/debug/QcAppLogging.h"
#include "clientcore/settings/QcScreenChoice.h"

#include <QGuiApplication>
#include <QScreen>
#include <QWindow>

using namespace Qt::StringLiterals;

using WindowMode = QcDisplayOptions::WindowMode;

namespace {

// How long the window's state reports after apply() belong to its switch (macOS animates the
// fullscreen space for about 0.7 s).
constexpr qint64 settleMs = 1500;

} // namespace

QcWindowPlacement::QcWindowPlacement(QObject *parent)
    : QObject(parent)
{
}

void QcWindowPlacement::setWindow(QWindow *window)
{
    if (m_window) {
        disconnect(m_window, nullptr, this, nullptr);
    }
    m_window = window;
    if (!m_window) {
        return;
    }
    m_defaultSize = m_window->size();
    m_fullscreen = m_window->windowStates().testFlag(Qt::WindowFullScreen);
    connect(m_window, &QWindow::windowStateChanged, this, &QcWindowPlacement::onWindowStateChanged);
}

QStringList QcWindowPlacement::screenNames()
{
    QStringList names;
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (const QScreen *screen : screens) {
        names.append(screen->name());
    }
    return names;
}

QStringList QcWindowPlacement::screenLabels()
{
    QStringList labels;
    const QList<QScreen *> screens = QGuiApplication::screens();
    for (qsizetype i = 0; i < screens.size(); ++i) {
        const QScreen *screen = screens[i];
        // Device pixels, as players know their monitors.
        const QSize pixels = screen->size() * screen->devicePixelRatio();
        labels.append(u"%1: %2 (%3 × %4)"_s.arg(i + 1).arg(screen->name()).arg(pixels.width()).arg(pixels.height()));
    }
    return labels;
}

QScreen *QcWindowPlacement::screenNamed(const QString &name)
{
    const QList<QScreen *> screens = QGuiApplication::screens();
    if (const std::optional<qsizetype> index = QcDisplayOptions::screenIndex(screenNames(), name)) {
        return screens[*index];
    }
    if (!name.isEmpty()) {
        qCWarning(lcDisplay).noquote() << u"The screen '%1' is not connected; using the primary screen"_s.arg(name);
    }
    return QGuiApplication::primaryScreen();
}

void QcWindowPlacement::apply(const QcDisplayOptions &requested)
{
    // A copy, taken before the window reports anything (see the header): its signals reach the
    // caller, who may change `requested` meanwhile.
    const QcDisplayOptions options = requested; // NOLINT(performance-unnecessary-copy-initialization): see above
    QScreen *screen = screenNamed(options.screen);
    if (!m_window || !screen) {
        return;
    }

    m_mode = options.windowMode;
    m_sinceApply.start();
    // Another screen or another mode starts from a normal window.
    const bool fullscreen = m_window->windowStates().testFlag(Qt::WindowFullScreen);
    const bool fullscreenThere = fullscreen && m_window->screen() == screen;
    if (fullscreen && !(m_mode == WindowMode::Fullscreen && fullscreenThere)) {
        m_window->showNormal();
    }

    switch (m_mode) {
    case WindowMode::Windowed:
        m_window->setFlag(Qt::FramelessWindowHint, false);
        m_window->setScreen(screen);
        m_window->setGeometry(
            QcDisplayOptions::windowedFrame(options.windowGeometry, screen->availableGeometry(), m_defaultSize, m_window->frameMargins()));
        m_window->showNormal();
        break;
    case WindowMode::Borderless:
        m_window->setFlag(Qt::FramelessWindowHint, true);
        m_window->setScreen(screen);
        m_window->setGeometry(screen->geometry());
        m_window->showNormal();
        break;
    case WindowMode::Fullscreen:
        m_window->setFlag(Qt::FramelessWindowHint, false);
        if (!fullscreenThere) {
            m_window->setScreen(screen);
            m_window->setGeometry(QcScreenChoice::placeInside(screen->availableGeometry(), m_window->size()));
        }
        m_window->showFullScreen();
        break;
    }
    qCInfo(lcDisplay).noquote() << u"Window %1 on screen \"%2\""_s.arg(QcDisplayOptions::modeName(m_mode), screen->name());
}

bool QcWindowPlacement::settling() const
{
    return m_sinceApply.isValid() && m_sinceApply.elapsed() < settleMs;
}

QRect QcWindowPlacement::windowedFrame() const
{
    if (!m_window || !m_window->isVisible() || m_mode != WindowMode::Windowed || m_window->windowStates() != Qt::WindowNoState) {
        return {};
    }
    return m_window->geometry();
}

QString QcWindowPlacement::screenName() const
{
    return m_window && m_window->screen() ? m_window->screen()->name() : QString();
}

void QcWindowPlacement::onWindowStateChanged(Qt::WindowState state)
{
    const bool fullscreen = state == Qt::WindowFullScreen;
    if (fullscreen == m_fullscreen) {
        return;
    }
    m_fullscreen = fullscreen;
    // The platform reports the states of apply()'s steps later, some of them after the switch
    // (on macOS it is animated): only a change well after it is the platform's own.
    if (fullscreen == (m_mode == WindowMode::Fullscreen) || settling()) {
        return;
    }
    if (fullscreen) {
        m_mode = WindowMode::Fullscreen;
    }
    emit fullscreenSwitched(fullscreen);
}
