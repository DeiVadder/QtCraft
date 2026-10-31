// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/session/QcAfterFrame.h"

#include <QQuickWindow>

namespace {

// Frames to see after a change before it is surely on screen.
constexpr int framesToWait = 2;

} // namespace

QcAfterFrame::QcAfterFrame(QObject *parent)
    : QObject(parent)
{
    m_fallback.setSingleShot(true);
    connect(&m_fallback, &QTimer::timeout, this, &QcAfterFrame::fire);
}

void QcAfterFrame::setWindow(QQuickWindow *window)
{
    if (m_window == window) {
        return;
    }

    disconnect(m_connection);
    m_window = window;
    if (window) {
        // frameSwapped comes from the render thread with the threaded render loop.
        m_connection = connect(window, &QQuickWindow::frameSwapped, this, &QcAfterFrame::onFrameSwapped, Qt::QueuedConnection);
    }
}

void QcAfterFrame::run(std::function<void()> step)
{
    setStep(std::move(step));
    setFramesLeft(framesToWait);
    if (window()) {
        window()->update();
    }
    m_fallback.start(window() ? fallbackMs : 0);
}

void QcAfterFrame::cancel()
{
    setStep(nullptr);
    m_fallback.stop();
}

void QcAfterFrame::onFrameSwapped()
{
    // A queued frame may arrive after the window was replaced or destroyed; the fallback then runs
    // the step.
    if (!pending() || !window() || sender() != window()) {
        return;
    }

    setFramesLeft(framesLeft() - 1);
    if (framesLeft() > 0) {
        window()->update();
        return;
    }
    fire();
}

void QcAfterFrame::fire()
{
    m_fallback.stop();
    // The step may start the next one.
    const std::function<void()> next = step();
    setStep(nullptr);
    if (next) {
        next();
    }
}
