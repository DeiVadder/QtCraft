#include "app/display/QcFrameLimiter.h"

#include <QQuickWindow>

QcFrameLimiter::QcFrameLimiter(QObject *parent)
    : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &QcFrameLimiter::onTimer);
    m_clock.start();
}

void QcFrameLimiter::setLimit(int framesPerSecond)
{
    if (framesPerSecond == m_pacer.limit()) {
        return;
    }
    m_pacer.setLimit(framesPerSecond);
    // A frame waiting for the old limit is requested now; the new grid starts with it.
    if (m_timer.isActive()) {
        m_timer.stop();
        onTimer();
    }
}

bool QcFrameLimiter::frameDue(QQuickWindow *window)
{
    if (m_pacer.frameDue(nowNs())) {
        return true;
    }
    requestNextFrame(window);
    return false;
}

void QcFrameLimiter::requestNextFrame(QQuickWindow *window)
{
    if (!window) {
        return;
    }
    m_window = window;
    const qint64 delayNs = m_pacer.delayNs(nowNs());
    if (delayNs == 0) {
        window->update();
        return;
    }
    if (!m_timer.isActive()) {
        // Rounded up: a millisecond early would be an early frame again.
        m_timer.start(int((delayNs + 999'999) / 1'000'000));
    }
}

void QcFrameLimiter::onTimer()
{
    if (m_window) {
        m_window->update();
    }
}
