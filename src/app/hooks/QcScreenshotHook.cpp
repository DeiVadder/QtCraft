#include "QcScreenshotHook.h"

#include <QCoreApplication>
#include <QImage>
#include <QLoggingCategory>
#include <QQuickWindow>
#include <QTimer>

#include <utility>

namespace {
Q_LOGGING_CATEGORY(lcScreenshot, "qtcraft.screenshot")
}

QcScreenshotHook::QcScreenshotHook(QQuickWindow *window, QString filePath, int frameCount, QObject *parent)
    : QObject(parent)
    , m_window(window)
    , m_filePath(std::move(filePath))
    , m_remainingFrames(frameCount)
{
    connect(window, &QQuickWindow::frameSwapped, this, &QcScreenshotHook::onFrameSwapped, Qt::QueuedConnection);
}

QcScreenshotHook *QcScreenshotHook::createFromEnvironment(QQuickWindow *window, QObject *parent)
{
    const QString filePath = qEnvironmentVariable("QTCRAFT_SCREENSHOT");
    if (filePath.isEmpty() || !window) {
        return nullptr;
    }

    bool ok = false;
    int frames = qEnvironmentVariableIntValue("QTCRAFT_SCREENSHOT_FRAMES", &ok);
    if (!ok || frames <= 0) {
        frames = defaultFrameCount;
    }
    return new QcScreenshotHook(window, filePath, frames, parent);
}

void QcScreenshotHook::hold()
{
    ++m_holdCount;
}

void QcScreenshotHook::release()
{
    if (m_holdCount > 0) {
        --m_holdCount;
    }
    if (m_window) {
        m_window->update();
    }
}

void QcScreenshotHook::onFrameSwapped()
{
    if (m_done || !m_window) {
        return;
    }

    if (m_holdCount > 0 || --m_remainingFrames > 0) {
        // Keep frames coming even when the scene is static.
        m_window->update();
        return;
    }

    m_done = true;
    const QImage image = m_window->grabWindow();
    if (image.isNull() || !image.save(m_filePath)) {
        qCCritical(lcScreenshot) << "Failed to save screenshot to" << m_filePath;
        QCoreApplication::exit(1);
        return;
    }

    qCInfo(lcScreenshot) << "Saved screenshot to" << m_filePath;
    QTimer::singleShot(0, qApp, [] { QCoreApplication::quit(); });
}
