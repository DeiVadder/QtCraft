// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcSettingsController.h"
#include "app/display/QcDisplaySettings.h"
#include "clientcore/settings/QcKeyControls.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>

using namespace Qt::StringLiterals;

using WindowMode = QcDisplaySettings::WindowMode;
using FrameLimit = QcDisplaySettings::FrameLimit;

// The display settings of the game (GameController.settings.display, decision E-835) on the
// offscreen platform: stored at once and read again, rejected values with a warning, the window
// modes on a real (offscreen) window with the fullscreen key, and the frame limit for the world in
// the foreground and behind a menu or another window.
class TestDisplaySettings : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        QDir().mkpath(QFileInfo(QcSettingsController::settingsPath()).path());
        QFile::remove(QcSettingsController::settingsPath());
    }

    void storedAtOnceAndReadAgain()
    {
        const QcKeyControls keys;
        {
            QcDisplaySettings display(keys);
            QCOMPARE(display.windowMode(), WindowMode::Windowed);
            QCOMPARE(display.frameLimit(), FrameLimit::Unlimited);
            QCOMPARE(display.backgroundFrameLimit(), FrameLimit::Fps30);
            QVERIFY(display.vsync());
            QVERIFY(!display.restartNeeded());
            QVERIFY(!display.controlsWindow());

            QSignalSpy changed(&display, &QcDisplaySettings::changed);
            display.setFrameLimit(FrameLimit::Fps144);
            display.setBackgroundFrameLimit(FrameLimit::Fps60);
            display.setVsync(false);
            display.setWindowMode(WindowMode::Borderless);
            QCOMPARE(changed.count(), 4);
            // VSync belongs to the window made at the start.
            QVERIFY(display.restartNeeded());
            // The same values again change nothing.
            display.setFrameLimit(FrameLimit::Fps144);
            display.setWindowMode(WindowMode::Borderless);
            QCOMPARE(changed.count(), 4);
        }

        const QSettings file(QcSettingsController::settingsPath(), QSettings::IniFormat);
        QCOMPARE(file.value(u"display/frameLimit"_s).toInt(), 144);
        QCOMPARE(file.value(u"display/windowMode"_s).toString(), u"borderless"_s);

        const QcDisplaySettings again(keys);
        QCOMPARE(again.frameLimit(), FrameLimit::Fps144);
        QCOMPARE(again.backgroundFrameLimit(), FrameLimit::Fps60);
        QCOMPARE(again.windowMode(), WindowMode::Borderless);
        QVERIFY(!again.vsync());
    }

    void rejectedValuesAreWarnedAbout()
    {
        const QcKeyControls keys;
        QcDisplaySettings display(keys);
        QSignalSpy changed(&display, &QcDisplaySettings::changed);

        QTest::ignoreMessage(QtWarningMsg, "Frame limit 45 for frameLimit rejected: not one of the choices");
        display.setFrameLimit(static_cast<FrameLimit>(45));
        QTest::ignoreMessage(QtWarningMsg, "Frame limit -1 for backgroundFrameLimit rejected: not one of the choices");
        display.setBackgroundFrameLimit(static_cast<FrameLimit>(-1));
        QTest::ignoreMessage(QtWarningMsg, "Window mode 7 rejected: unknown");
        display.setWindowMode(static_cast<WindowMode>(7));
        const int screens = static_cast<int>(QGuiApplication::screens().size());
        QTest::ignoreMessage(QtWarningMsg, qPrintable(u"Screen %1 rejected: there are %1 screens"_s.arg(screens)));
        display.setScreen(screens);
        QTest::ignoreMessage(QtWarningMsg, qPrintable(u"Screen -1 rejected: there are %1 screens"_s.arg(screens)));
        display.setScreen(-1);

        QCOMPARE(changed.count(), 0);
        QCOMPARE(display.frameLimit(), FrameLimit::Unlimited);
        QCOMPARE(display.backgroundFrameLimit(), FrameLimit::Fps30);
        QCOMPARE(display.windowMode(), WindowMode::Windowed);
        QCOMPARE(display.screens().size(), screens);
        QCOMPARE(display.screen(), 0);
    }

    // A screen that is no longer connected: the window opens on the primary screen.
    void unknownScreenFallsBackToThePrimary()
    {
        {
            QSettings file(QcSettingsController::settingsPath(), QSettings::IniFormat);
            file.setValue(u"display/screen"_s, u"Gone Monitor"_s);
            file.setValue(u"display/windowGeometry"_s, QRect(5000, 5000, 900, 700));
        }
        const QcKeyControls keys;
        QcDisplaySettings display(keys);
        QQuickWindow window;
        window.resize(320, 200);
        QTest::ignoreMessage(QtWarningMsg, "The screen 'Gone Monitor' is not connected; using the primary screen");
        display.showWindow(window);
        QVERIFY(window.isVisible());
        QCOMPARE(window.screen(), QGuiApplication::primaryScreen());
        // The stored frame lay on the missing screen: the default size, centred.
        QCOMPARE(window.geometry().center(), window.screen()->availableGeometry().center());
        QCOMPARE(window.size(), QSize(320, 200));
    }

    // Borderless covers the screen without a frame, the fullscreen key switches fullscreen and back
    // to the mode before it, and the window returns to its frame in windowed mode.
    void windowModesOnAWindow()
    {
        const QcKeyControls keys;
        QcDisplaySettings display(keys);
        QQuickWindow window;
        window.resize(320, 200);
        display.showWindow(window);
        QVERIFY(display.controlsWindow());
        QVERIFY(!window.flags().testFlag(Qt::FramelessWindowHint));
        window.setGeometry(QRect(40, 30, 400, 300));
        const QRect screen = window.screen()->geometry();

        display.setWindowMode(WindowMode::Borderless);
        QVERIFY(window.flags().testFlag(Qt::FramelessWindowHint));
        QCOMPARE(window.geometry(), screen);
        QVERIFY(!window.windowStates().testFlag(Qt::WindowFullScreen));

        QTest::keyClick(&window, Qt::Key_F11);
        QCOMPARE(display.windowMode(), WindowMode::Fullscreen);
        QVERIFY(window.windowStates().testFlag(Qt::WindowFullScreen));
        QVERIFY(!window.flags().testFlag(Qt::FramelessWindowHint));

        display.toggleFullscreen();
        QCOMPARE(display.windowMode(), WindowMode::Borderless);
        QVERIFY(!window.windowStates().testFlag(Qt::WindowFullScreen));
        QVERIFY(window.flags().testFlag(Qt::FramelessWindowHint));

        display.setWindowMode(WindowMode::Windowed);
        QVERIFY(!window.flags().testFlag(Qt::FramelessWindowHint));
        QCOMPARE(window.geometry(), QRect(40, 30, 400, 300));

        // Fullscreen from windowed remembers the frame; the key returns to it.
        display.toggleFullscreen();
        QVERIFY(window.windowStates().testFlag(Qt::WindowFullScreen));
        display.toggleFullscreen();
        QCOMPARE(display.windowMode(), WindowMode::Windowed);
        QCOMPARE(window.geometry(), QRect(40, 30, 400, 300));
        const QSettings file(QcSettingsController::settingsPath(), QSettings::IniFormat);
        QCOMPARE(file.value(u"display/windowGeometry"_s).toRect(), QRect(40, 30, 400, 300));
    }

    // The window reports a frame of the screen's size while it leaves fullscreen (macOS does so
    // during its animation): the stored windowed frame stays, and the window returns to it.
    void framesDuringASwitchAreNotStored()
    {
        const QcKeyControls keys;
        QcDisplaySettings display(keys);
        QQuickWindow window;
        window.resize(320, 200);
        display.showWindow(window);
        const QRect frame(40, 30, 400, 300);
        window.setGeometry(frame);
        display.toggleFullscreen();
        QVERIFY(window.windowStates().testFlag(Qt::WindowFullScreen));
        QCOMPARE(display.options().windowGeometry, frame);

        // Leaving fullscreen: when the state changes to normal, the window is as large as the screen.
        const QRect screen = window.screen()->geometry();
        bool reported = false;
        const QMetaObject::Connection report = connect(&window, &QWindow::windowStateChanged, &window, [&](Qt::WindowState state) {
            if (state == Qt::WindowNoState && !reported) {
                reported = true;
                window.setGeometry(screen);
            }
        });
        display.toggleFullscreen();
        disconnect(report);
        QVERIFY(reported);
        QCOMPARE(display.windowMode(), WindowMode::Windowed);
        QCOMPARE(window.geometry(), frame);
        QCOMPARE(display.options().windowGeometry, frame);

        // Moves right after a switch belong to it (QcWindowPlacement::settling, 1.5 s); a move
        // after that is the player's and is noted once the window rests.
        window.setGeometry(screen);
        QTest::qWait(1600);
        QCOMPARE(display.options().windowGeometry, frame);
        const QRect moved(60, 50, 420, 310);
        window.setGeometry(moved);
        QTRY_COMPARE_WITH_TIMEOUT(display.options().windowGeometry, moved, 10000);
    }

    // A screen that goes away takes the window with it: the window is placed again, on the screen
    // it falls back to.
    void removedScreenPlacesTheWindowAgain()
    {
        const QcKeyControls keys;
        QcDisplaySettings display(keys);
        QQuickWindow window;
        window.resize(320, 200);
        display.showWindow(window);
        display.setWindowMode(WindowMode::Borderless);
        const QRect screen = window.screen()->geometry();
        window.setGeometry(QRect(10, 10, 200, 100));

        // The offscreen platform cannot unplug a screen; the signal stands in for it.
        QSignalSpy screens(&display, &QcDisplaySettings::screensChanged);
        emit qGuiApp->screenRemoved(window.screen());
        QCOMPARE(screens.count(), 1);
        QTRY_COMPARE(window.geometry(), screen);
    }

    // While the controls page waits for a key, the fullscreen key is bound, not used.
    void fullscreenKeyLeavesTheKeyCaptureAlone()
    {
        const QcKeyControls keys;
        QcDisplaySettings display(keys);
        QQuickWindow window;
        display.showWindow(window);
        QQuickItem capture(window.contentItem());
        capture.setObjectName(u"keyCapture"_s);
        capture.setFocus(true);
        window.requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QCOMPARE(window.activeFocusItem(), &capture);
        QTest::keyClick(&window, Qt::Key_F11);
        QCOMPARE(display.windowMode(), WindowMode::Windowed);
    }

    // Scripted runs draw without a limit; a window handed over gets the game's limit while the
    // world is in the foreground and the background limit otherwise.
    void frameLimitFollowsForeground()
    {
        const QcKeyControls keys;
        QcDisplaySettings display(keys);
        display.setFrameLimit(FrameLimit::Fps144);
        display.setForeground(false);
        QCOMPARE(display.activeFrameLimit(), 0);

        QQuickWindow window;
        QSignalSpy active(&display, &QcDisplaySettings::activeFrameLimitChanged);
        display.showWindow(window);
        QCOMPARE(display.activeFrameLimit(), 30);
        display.setForeground(true);
        QCOMPARE(display.activeFrameLimit(), 144);
        display.setFrameLimit(FrameLimit::Unlimited);
        QCOMPARE(display.activeFrameLimit(), 0);
        display.setForeground(false);
        QCOMPARE(display.activeFrameLimit(), 30);
        display.setBackgroundFrameLimit(FrameLimit::Unlimited);
        QCOMPARE(display.activeFrameLimit(), 0);
        QCOMPARE(active.count(), 5);

        // At 30 frames a second a frame right after another is early.
        display.setBackgroundFrameLimit(FrameLimit::Fps30);
        QVERIFY(display.frames().frameDue(&window));
        QVERIFY(!display.frames().frameDue(&window));
    }
};

QTEST_MAIN(TestDisplaySettings)
#include "test_display_settings.moc"
