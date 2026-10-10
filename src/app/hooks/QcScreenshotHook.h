#pragma once

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

class QQuickWindow;

// Test hook: grabs the window after a number of rendered frames, saves it and quits the application.
// Enabled through QTCRAFT_SCREENSHOT=<file.png> (and optionally QTCRAFT_SCREENSHOT_FRAMES=<n>).
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

private:
    void onFrameSwapped();

    QPointer<QQuickWindow> m_window;
    std::function<QImage()> m_grabber;
    QString m_filePath;
    int m_remainingFrames = 0;
    int m_holdCount = 0;
    bool m_done = false;
};
