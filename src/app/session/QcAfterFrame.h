// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QPointer>
#include <QTimer>

#include <functional>

class QQuickWindow;

// Runs one step of a world start once the window has shown what changed before it (decision
// E-943): the loading screen appears in the frame after the click, and the step that blocks the
// GUI thread (loading the content, opening the world) runs only then. Waits for two presented
// frames (the first may have been synchronised before the change); a window that draws nothing
// (hidden, minimised) runs the step after `fallbackMs`. One step waits at a time; a new one
// replaces it, cancel() drops it. Lives in the GUI thread.
class QcAfterFrame : public QObject
{
    Q_OBJECT

public:
    static constexpr int fallbackMs = 300;

    explicit QcAfterFrame(QObject *parent = nullptr);

    void setWindow(QQuickWindow *window);
    // Runs `step` after the next presented frames (see above).
    void run(std::function<void()> step);
    void cancel();
    bool pending() const { return static_cast<bool>(step()); }

private:
    QQuickWindow *window() const { return m_window; }
    const std::function<void()> &step() const { return m_step; }
    void setStep(std::function<void()> step) { m_step = std::move(step); }
    int framesLeft() const { return m_framesLeft; }
    void setFramesLeft(int frames) { m_framesLeft = frames; }
    void onFrameSwapped();
    void fire();

    QPointer<QQuickWindow> m_window;
    QMetaObject::Connection m_connection;
    std::function<void()> m_step;
    int m_framesLeft = 0;
    // NOLINT(qc-member-access): a timer, started and stopped through its own API
    QTimer m_fallback;
};
