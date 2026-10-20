#include "app/hooks/QcScreenshotHook.h"

#include "app/debug/QcAppLogging.h"
#include "app/hooks/QcAccessibilityCheck.h"
#include "app/hooks/QcLayoutCheck.h"
#include "app/hooks/QcOffscreenRender.h"
#include "common/core/QcProcessMemory.h"

#include <QCoreApplication>
#include <QImage>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>

#include <utility>

using namespace Qt::StringLiterals;

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
    auto *hook = new QcScreenshotHook(window, filePath, frames, parent);
    hook->m_checkLayout = qEnvironmentVariableIntValue("QTCRAFT_LAYOUT_CHECK") == 1;
    hook->m_logUiStats = qEnvironmentVariableIntValue("QTCRAFT_UI_STATS") == 1;
    hook->m_checkAccessibility = qEnvironmentVariableIntValue("QTCRAFT_ACCESSIBILITY_CHECK") == 1;
    return hook;
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

void QcScreenshotHook::setGrabber(std::function<QImage()> grabber)
{
    m_grabber = std::move(grabber);
}

std::function<QImage()> QcScreenshotHook::grabberFor(QQuickWindow &window, QcOffscreenRender *offscreen)
{
    if (offscreen) {
        return [offscreen] { return offscreen->grab(); };
    }
    return [window = QPointer<QQuickWindow>(&window)] { return window ? window->grabWindow() : QImage(); };
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
    if (m_checkLayout) {
        logLayoutCheck();
    }
    if (m_logUiStats) {
        logUiStats();
    }
    if (m_checkAccessibility) {
        logAccessibilityCheck();
    }
    const QImage image = m_grabber ? m_grabber() : m_window->grabWindow();
    if (image.isNull() || !image.save(m_filePath)) {
        qCCritical(lcScreenshot) << "Failed to save screenshot to" << m_filePath;
        QCoreApplication::exit(1);
        return;
    }

    qCInfo(lcScreenshot) << "Saved screenshot to" << m_filePath;
    QTimer::singleShot(0, qApp, [] { QCoreApplication::quit(); });
}

void QcScreenshotHook::logLayoutCheck() const
{
    const QQuickItem *content = m_window->contentItem();
    const QStringList outside = QcLayoutCheck::itemsOutside(*content, QRectF(QPointF(), content->size()));
    for (const QString &item : outside) {
        qCWarning(lcScreenshot).noquote() << "Outside the window:" << item;
    }
    qCInfo(lcScreenshot).noquote() << u"Layout check: %1 items outside the window %2x%3"_s.arg(outside.size()).arg(content->width()).arg(content->height());

    const QStringList overlaps = QcLayoutCheck::chatOverlaps(*content);
    for (const QString &row : overlaps) {
        qCWarning(lcScreenshot).noquote() << "Chat lines overlap:" << row;
    }
    qCInfo(lcScreenshot).noquote() << u"HUD check: chat lines overlap %1 status rows"_s.arg(overlaps.size());
}

void QcScreenshotHook::logUiStats() const
{
    // The window's QML objects keep their QObject parents when offscreen runs move the items into
    // another window, so the count is the same in both.
    const qsizetype objects = m_window->findChildren<QObject *>().size();
    const qsizetype items = m_window->findChildren<QQuickItem *>().size();
    qCInfo(lcScreenshot).noquote() << u"UI stats: objects %1 items %2 memory %3 MB"_s.arg(objects).arg(items).arg(QcProcessMemory::currentMb(), 0, 'f', 1);
}

void QcScreenshotHook::logAccessibilityCheck() const
{
    const QStringList unnamed = QcAccessibilityCheck::unnamedControls(*m_window->contentItem());
    for (const QString &control : unnamed) {
        qCWarning(lcScreenshot).noquote() << "No accessible name:" << control;
    }
    qCInfo(lcScreenshot).noquote() << u"Accessibility check: %1 controls without a name"_s.arg(unnamed.size());
}
