#include "app/hooks/QcScreenshotHook.h"

#include "app/debug/QcAppLogging.h"
#include "app/hooks/QcAccessibilityCheck.h"
#include "app/hooks/QcLayoutCheck.h"
#include "app/hooks/QcOffscreenRender.h"
#include "app/hooks/QcPendingScreens.h"
#include "common/core/QcProcessMemory.h"

#include <QCoreApplication>
#include <QImage>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>

#include <utility>

using namespace Qt::StringLiterals;

QcScreenshotHook::QcScreenshotHook(QQuickWindow *window, QString filePath, int frameCount, Checks checks, QObject *parent)
    : QObject(parent)
    , m_window(window)
    , m_filePath(std::move(filePath))
    , m_frameCount(frameCount)
    , m_remainingFrames(frameCount)
    , m_checks(checks)
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
    const Checks checks{qEnvironmentVariableIntValue("QTCRAFT_LAYOUT_CHECK") == 1, qEnvironmentVariableIntValue("QTCRAFT_UI_STATS") == 1,
                        qEnvironmentVariableIntValue("QTCRAFT_ACCESSIBILITY_CHECK") == 1, qEnvironmentVariableIntValue("QTCRAFT_CHECK_PAD_STEPS") == 1};
    auto *hook = new QcScreenshotHook(window, filePath, frames, checks, parent);
    hook->m_loggedItems = qEnvironmentVariable("QTCRAFT_LOG_ITEMS").split(u',', Qt::SkipEmptyParts);
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

    if (m_holdCount > 0 || --m_remainingFrames > 0 || waitingForScreens()) {
        // Keep frames coming even when the scene is static.
        m_window->update();
        return;
    }

    m_done = true;
    if (checks().layout) {
        logLayoutCheck();
    }
    if (checks().uiStats) {
        logUiStats();
    }
    if (checks().accessibility) {
        logAccessibilityCheck();
    }
    logItems();
    const QImage image = m_grabber ? m_grabber() : m_window->grabWindow();
    if (image.isNull() || !image.save(m_filePath)) {
        qCCritical(lcScreenshot) << "Failed to save screenshot to" << m_filePath;
        QCoreApplication::exit(1);
        return;
    }

    qCInfo(lcScreenshot) << "Saved screenshot to" << m_filePath;
    QTimer::singleShot(0, qApp, [] { QCoreApplication::quit(); });
}

void QcScreenshotHook::checkPadStep() const
{
    // After the picture the window is closing; its checks already ran.
    if (!checks().padSteps || m_done || !m_window) {
        return;
    }
    if (checks().layout) {
        logLayoutCheck();
    }
    if (checks().accessibility) {
        logAccessibilityCheck();
    }
}

bool QcScreenshotHook::waitingForScreens()
{
    if (!QcPendingScreens::any(*m_window)) {
        return false;
    }
    if (++m_screenWaitFrames <= QcPendingScreens::maxWaitFrames) {
        return true;
    }

    qCWarning(lcScreenshot) << "A shown screen is still being made after" << QcPendingScreens::maxWaitFrames << "more frames; taking the picture";
    return false;
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
    qCInfo(lcScreenshot).noquote() << u"HUD parts shown: %1"_s.arg(QcLayoutCheck::hudPartsShown(*content));

    const QStringList advancements = QcLayoutCheck::advancementOverlaps(*content);
    for (const QString &pair : advancements) {
        qCWarning(lcScreenshot).noquote() << "Advancements overlap:" << pair;
    }
    qCInfo(lcScreenshot).noquote() << u"Advancement check: %1 overlapping advancements"_s.arg(advancements.size());
}

void QcScreenshotHook::logItems() const
{
    const QQuickItem *content = m_window->contentItem();
    for (const QString &name : m_loggedItems) {
        // The items keep their QObject parents when offscreen runs move them into another window.
        const auto *item = m_window->findChild<QQuickItem *>(name);
        if (!item) {
            qCInfo(lcScreenshot).noquote() << u"Item %1: not found"_s.arg(name);
            continue;
        }
        const QRectF rect = item->mapRectToScene(QRectF(0.0, 0.0, item->width(), item->height()));
        // A control's text, where it has one (a button's), for checks of what a screen offers; a
        // text field also what it shows when empty (the seed of a new world, decision E-930).
        const QVariant text = item->property("text");
        const QVariant placeholder = item->property("placeholderText");
        QString textPart = text.isValid() ? u" text \"%1\""_s.arg(text.toString()) : QString();
        if (placeholder.isValid()) {
            textPart += u" placeholder \"%1\""_s.arg(placeholder.toString());
        }
        qCInfo(lcScreenshot).noquote() << u"Item %1: x %2 y %3 width %4 height %5 in the window %6x%7 visible %8%9"_s.arg(name)
                                              .arg(rect.x())
                                              .arg(rect.y())
                                              .arg(rect.width())
                                              .arg(rect.height())
                                              .arg(content->width())
                                              .arg(content->height())
                                              .arg(item->isVisible() ? u"yes"_s : u"no"_s, textPart);
    }
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
