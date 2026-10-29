// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/preview/QcFramePacer.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QTimer>

#include <functional>

// Paces the frames the running world asks for (decision E-835): QcFrameDriver asks frameDue()
// at the start of every frame and requestNextFrame() at its end. Without a limit both pass through
// (the window draws every frame, as fast as VSync lets it); with one, a frame that comes early (an
// item changed during the previous one asked for it) leaves the scene alone, and the next frame is
// requested by a precise timer when it is due. Independent of the graphics API.
class QcFrameLimiter : public QObject
{
    Q_OBJECT

public:
    explicit QcFrameLimiter(QObject *parent = nullptr);

    // Frames per second; 0 is no limit.
    void setLimit(int framesPerSecond);
    int limit() const { return m_pacer.limit(); }

    // Whether the frame starting now is to be drawn; when it says yes, it counts as drawn. An early
    // frame makes sure the next one is requested.
    bool frameDue(QQuickWindow *window);
    // Asks `window` for the next frame: at once without a limit, else when it is due.
    void requestNextFrame(QQuickWindow *window);
    // The clock in nanoseconds the frames are paced by; tests pass their own. Empty: the monotonic
    // clock of the process.
    void setClock(std::function<qint64()> clock) { m_testClock = std::move(clock); }

private:
    void onTimer();
    qint64 nowNs() const { return m_testClock ? m_testClock() : m_clock.nsecsElapsed(); }

    QcFramePacer m_pacer;
    QPointer<QQuickWindow> m_window;
    QTimer m_timer;
    QElapsedTimer m_clock;
    std::function<qint64()> m_testClock;
};
