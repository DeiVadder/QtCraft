#pragma once

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>

class QcOffscreenRender;
class QQuickWindow;

// Test hook: grabs the window after a number of rendered frames, saves it and quits the application.
// Enabled through QTCRAFT_SCREENSHOT=<file.png> (and optionally QTCRAFT_SCREENSHOT_FRAMES=<n>).
// With QTCRAFT_LAYOUT_CHECK=1 it logs, just before the picture, every shown item that reaches
// outside the window (QcLayoutCheck) and a summary line "Layout check: <n> items outside the window".
// With QTCRAFT_UI_STATS=1 it logs, also just before the picture, the line "UI stats: objects <n>
// items <n> memory <MB> MB": the QML objects below the window, its Qt Quick items and the
// resident memory of the process (decision E-690). With QTCRAFT_ACCESSIBILITY_CHECK=1 it logs
// every shown control without an accessible name or role (QcAccessibilityCheck) and a summary line
// "Accessibility check: <n> controls without a name" (decision E-725). With
// QTCRAFT_LOG_ITEMS=<objectName>,... it logs "Item <name>: x <x> y <y> width <w> height <h> in the
// window <w>x<h>" for each named item, in window coordinates (decision E-875), or "Item <name>:
// not found". The picture waits for shown screens that are still being made (LazyScreen).
class QcScreenshotHook : public QObject
{
    Q_OBJECT

public:
    static constexpr int defaultFrameCount = 90;

    QcScreenshotHook(QQuickWindow *window, QString filePath, int frameCount, QObject *parent = nullptr);

    // Creates the hook from the environment; returns nullptr when the hook is not requested.
    static QcScreenshotHook *createFromEnvironment(QQuickWindow *window, QObject *parent);

    // Delays the capture until release() is called, e.g. until a world finished loading.
    void hold();
    void release();
    // Takes the picture some other way than QQuickWindow::grabWindow() (offscreen rendering).
    void setGrabber(std::function<QImage()> grabber);
    // How a picture of the game is taken: from the offscreen rendering when there is one, else
    // QQuickWindow::grabWindow() of `window`. Also the photo mode's screenshots (QcPhotoMode).
    static std::function<QImage()> grabberFor(QQuickWindow &window, QcOffscreenRender *offscreen);

private:
    void onFrameSwapped();
    // Whether a shown LazyScreen is still being made and the picture should wait for it
    // (QcPendingScreens, at most maxWaitFrames frames).
    [[nodiscard]] bool waitingForScreens();
    void logLayoutCheck() const;
    void logUiStats() const;
    void logAccessibilityCheck() const;
    void logItems() const;

    QPointer<QQuickWindow> m_window;
    std::function<QImage()> m_grabber;
    QString m_filePath;
    // QTCRAFT_LOG_ITEMS.
    QStringList m_loggedItems;
    int m_remainingFrames = 0;
    int m_holdCount = 0;
    int m_screenWaitFrames = 0;
    bool m_done = false;
    bool m_checkLayout = false;
    bool m_logUiStats = false;
    bool m_checkAccessibility = false;
};
