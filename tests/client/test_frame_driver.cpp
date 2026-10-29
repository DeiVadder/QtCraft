// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcHandbookController.h"
#include "app/controllers/QcHudState.h"
#include "app/controllers/QcScreenState.h"
#include "app/controllers/QcSettingsController.h"
#include "app/controllers/QcWorldLibraryController.h"
#include "app/display/QcDisplaySettings.h"
#include "app/hooks/QcBenchmarkRun.h"
#include "app/hooks/QcStartupHooks.h"
#include "app/input/QcGameInput.h"
#include "app/models/QcItemIcons.h"
#include "app/preview/QcPreviewSource.h"
#include "app/rendering/QcRenderBridge.h"
#include "app/session/QcFrameDriver.h"
#include "app/session/QcSessionParts.h"
#include "audio/QcAudioEngine.h"
#include "clientcore/input/QcGamepadControls.h"
#include "clientcore/meshing/QcMeshScheduler.h"
#include "clientcore/session/QcClientSession.h"
#include "QcTestUtil.h"
#include "server/core/QcServerConfig.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worlds/QcLocalServerHost.h"

#include <QQuickWindow>
#include <QTest>

using namespace Qt::StringLiterals;

using FrameLimit = QcDisplaySettings::FrameLimit;

namespace {

constexpr int timeoutMs = 30000;
constexpr qint64 msNs = 1'000'000;

} // namespace

// The frame rate limit on the world's frames (decision E-835): QcFrameDriver::onFrame, as the
// window calls it every frame, does the frame's work only when the limiter of the display settings
// lets it through. A session against an in-process server, the display settings controlling an
// offscreen window, and a clock the test moves.
class TestFrameDriver : public QObject
{
    Q_OBJECT

private:
    QcSessionParts m_parts;
    std::unique_ptr<QcAudioEngine> m_audio;
    std::unique_ptr<QcWorldLibraryController> m_library;
    std::unique_ptr<QcSettingsController> m_settings;
    QcItemIcons m_icons;
    std::unique_ptr<QcHandbookController> m_handbook;
    std::unique_ptr<QcScreenState> m_screens;
    std::unique_ptr<QcGameInput> m_input;
    QcGamepadControls m_gamepad;
    std::unique_ptr<QcPreviewSource> m_preview;
    std::unique_ptr<QcRenderBridge> m_render;
    std::unique_ptr<QcHudState> m_hud;
    std::unique_ptr<QcBenchmarkRun> m_benchmark;
    std::unique_ptr<QcStartupHooks> m_startup;
    std::unique_ptr<QcFrameDriver> m_driver;
    std::unique_ptr<QQuickWindow> m_window;
    qint64 m_nowNs = 0;

    QcDisplaySettings &display() const { return *m_settings->display(); }

    // Frames the driver handles of those the window calls at `nowMs`.
    qint64 framesAt(std::initializer_list<double> nowMs)
    {
        const qint64 before = m_driver->framesHandled();
        for (const double ms : nowMs) {
            m_nowNs = static_cast<qint64>(ms * static_cast<double>(msNs));
            m_driver->onFrame();
        }
        return m_driver->framesHandled() - before;
    }

private slots:
    void initTestCase()
    {
        std::shared_ptr<const QcContentRegistry> content = QcTestUtil::loadContent();
        QcServerConfig config;
        config.viewDistance = 2;
        config.worldName = u"FrameLimit"_s;
        m_parts.server = std::make_unique<QcLocalServerHost>(config, content, std::make_unique<QcTestTerrainGenerator>(content));
        m_parts.session = std::make_unique<QcClientSession>(m_parts.server->connectClient(), u"Pacer"_s);
        m_parts.session->start(QcClientSession::TickMode::Manual);
        QTRY_VERIFY_WITH_TIMEOUT(m_parts.session->state() == QcClientSession::State::Playing && m_parts.session->world(), timeoutMs);

        m_audio = std::make_unique<QcAudioEngine>();
        m_library = std::make_unique<QcWorldLibraryController>();
        m_settings = std::make_unique<QcSettingsController>(m_parts, *m_library, m_audio.get());
        m_handbook = std::make_unique<QcHandbookController>(m_parts, *m_settings, *m_library, m_icons);
        m_screens = std::make_unique<QcScreenState>(m_parts);
        m_input = std::make_unique<QcGameInput>(QcGameInput::Screens{*m_screens, *m_handbook}, m_parts, *m_settings, m_audio.get());
        m_preview = std::make_unique<QcPreviewSource>(m_parts, *m_library, *m_settings->character(), *m_settings->texturePacks());
        m_render = std::make_unique<QcRenderBridge>(m_parts, *m_settings);
        m_hud = std::make_unique<QcHudState>(m_parts, *m_settings);
        m_benchmark = std::make_unique<QcBenchmarkRun>(*m_screens, m_parts, *m_input, *m_settings);
        m_startup = std::make_unique<QcStartupHooks>(QcStartupHooks::Screens{*m_screens, *m_handbook, *m_input}, m_parts, m_benchmark.get());
        m_driver = std::make_unique<QcFrameDriver>(m_parts,
                                                   QcFrameDriver::Targets{*m_settings, *m_input, m_gamepad, *m_preview, *m_render, *m_hud,
                                                                          *m_benchmark, *m_startup},
                                                   [] { return true; });

        // A window handed to the display settings: its frames are limited.
        m_window = std::make_unique<QQuickWindow>();
        m_window->resize(320, 200);
        display().showWindow(*m_window);
        m_parts.window = m_window.get();
        display().frames().setClock([this] { return m_nowNs; });
        display().setForeground(true);
    }

    void cleanupTestCase()
    {
        m_parts.window = nullptr;
        display().frames().setClock({});
    }

    // Without a limit every frame is handled, however close together.
    void unlimitedHandlesEveryFrame()
    {
        display().setFrameLimit(FrameLimit::Unlimited);
        QCOMPARE(display().activeFrameLimit(), 0);
        QCOMPARE(framesAt({0.0, 1.0, 2.0, 3.0, 4.0}), 5);
    }

    // At 30 frames a second only frames a 30th of a second apart are handled; those in between
    // leave the world as it is.
    void limitSkipsEarlyFrames()
    {
        display().setFrameLimit(FrameLimit::Fps30);
        QCOMPARE(display().activeFrameLimit(), 30);
        QCOMPARE(framesAt({1000.0}), 1);
        QCOMPARE(framesAt({1005.0, 1016.0, 1030.0}), 0);
        QCOMPARE(framesAt({1033.4}), 1);
        // A second of frames at 120 Hz: 30 of them.
        QList<double> times;
        for (int frame = 1; frame <= 120; ++frame) {
            times.append(2000.0 + frame * 1000.0 / 120.0);
        }
        qint64 handled = 0;
        for (const double ms : times) {
            handled += framesAt({ms});
        }
        QVERIFY2(handled >= 29 && handled <= 31, qPrintable(QString::number(handled)));
    }

    // Behind a menu the background limit applies to the same frames.
    void backgroundLimitBehindAMenu()
    {
        display().setFrameLimit(FrameLimit::Unlimited);
        display().setBackgroundFrameLimit(FrameLimit::Fps30);
        display().setForeground(false);
        QCOMPARE(display().activeFrameLimit(), 30);
        QCOMPARE(framesAt({5000.0, 5010.0, 5020.0, 5034.0}), 2);
        display().setForeground(true);
        QCOMPARE(framesAt({5035.0, 5036.0}), 2);
    }
};

QTEST_MAIN(TestFrameDriver)
#include "test_frame_driver.moc"
