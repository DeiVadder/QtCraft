// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcRenderSettings.h"
#include "app/controllers/QcScreenState.h"
#include "app/controllers/QcSettingsController.h"
#include "app/controllers/QcWorldLibraryController.h"
#include "app/controllers/QcWorldPause.h"
#include "app/display/QcGraphicsGuard.h"
#include "app/rendering/QcWorldView.h"
#include "app/session/QcSessionParts.h"
#include "audio/QcAudioEngine.h"
#include "clientcore/meshing/QcMeshScheduler.h"
#include "clientcore/session/QcClientSession.h"
#include "worlds/QcLocalServerHost.h"

#include <QSignalSpy>
#include <QTest>

#include <memory>

using Overlay = QcScreenState::Overlay;
using Mode = QcRenderSettings::Mode;

// The graphics guard (decision E-963) without a window or session: the options it hands the world
// view after a lost device on a fake clock (cool-down with a timer that may fire early, the
// fall-back for the session with its notice, a new choice of the mode), and when the view stays
// still.
class TestGraphicsGuard : public QObject
{
    Q_OBJECT

private:
    QcSessionParts m_parts;
    QcAudioEngine m_audio;
    QcWorldLibraryController m_library;
    std::unique_ptr<QcSettingsController> m_settings;
    std::unique_ptr<QcScreenState> m_screens;
    std::unique_ptr<QcWorldPause> m_pause;
    std::unique_ptr<QcWorldView> m_view;
    std::unique_ptr<QcGraphicsGuard> m_guard;
    qint64 m_nowMs = 0;

    static constexpr qint64 coolDownMs = 50;

    bool drawsRealistic() const { return m_view->renderOptions().realistic(); }

private slots:
    void init()
    {
        m_nowMs = 0;
        m_settings = std::make_unique<QcSettingsController>(m_parts, m_library, &m_audio);
        m_screens = std::make_unique<QcScreenState>(m_parts);
        m_pause = std::make_unique<QcWorldPause>(m_parts, *m_screens, *m_settings);
        m_view = std::make_unique<QcWorldView>();
        m_guard = std::make_unique<QcGraphicsGuard>(*m_settings->rendering(), *m_screens, *m_pause);
        m_guard->setClock([this] { return m_nowMs; });
        m_guard->setCoolDownMs(coolDownMs);
        m_settings->rendering()->setMode(Mode::Realistic);
        m_guard->setView(m_view.get());
    }

    void cleanup()
    {
        m_guard.reset();
        m_view.reset();
        m_pause.reset();
        m_screens.reset();
        m_settings.reset();
    }

    void stillForTheScreens()
    {
        // The single-player pause menu pauses the world: still; in a world others play in it runs on.
        QVERIFY(QcGraphicsGuard::stillFor(true, Overlay::Pause, true));
        QVERIFY(!QcGraphicsGuard::stillFor(true, Overlay::Pause, false));
        QVERIFY(QcGraphicsGuard::stillFor(true, Overlay::Character, true));
        // Full-screen screens hide the world whether it pauses or not.
        QVERIFY(QcGraphicsGuard::stillFor(true, Overlay::Settings, false));
        QVERIFY(QcGraphicsGuard::stillFor(true, Overlay::Admin, false));
        QVERIFY(QcGraphicsGuard::stillFor(true, Overlay::Advancements, false));
        // The world itself and the screens over it opened from the game draw on.
        QVERIFY(!QcGraphicsGuard::stillFor(true, Overlay::None, false));
        QVERIFY(!QcGraphicsGuard::stillFor(true, Overlay::Inventory, false));
        // No world shown (main menu, loading): never still.
        QVERIFY(!QcGraphicsGuard::stillFor(false, Overlay::Pause, true));
        QVERIFY(!QcGraphicsGuard::stillFor(false, Overlay::Settings, false));
    }

    void theViewFollowsTheScreens()
    {
        QVERIFY(!m_view->isStill());
        m_screens->setPhase(QcScreenState::Phase::Playing);
        m_screens->setOverlay(Overlay::Settings);
        QVERIFY(m_guard->still());
        QVERIFY(m_view->isStill());
        // Without a session the world does not pause: the pause menu keeps it drawing.
        m_screens->setOverlay(Overlay::Pause);
        QVERIFY(!m_view->isStill());
        m_screens->setOverlay(Overlay::Settings);
        QVERIFY(m_view->isStill());
        m_screens->setOverlay(Overlay::None);
        QVERIFY(!m_view->isStill());
    }

    void aLossInTheRealisticModeCoolsDown()
    {
        QVERIFY(drawsRealistic());
        m_guard->deviceLost();
        QVERIFY(!drawsRealistic());
        QVERIFY(!m_guard->drawnOptions().realistic());

        // The timer fires (as a coarse one may) before the clock says the cool-down is over: the
        // view stays classic and the timer waits for the rest.
        m_nowMs = coolDownMs - 20;
        QTest::qWait(3 * static_cast<int>(coolDownMs));
        QVERIFY(!drawsRealistic());
        m_nowMs = coolDownMs;
        QTRY_VERIFY(drawsRealistic());
    }

    void aSecondLossFallsBackForTheSession()
    {
        const QSignalSpy notices(m_settings->rendering(), &QcRenderSettings::announced);
        m_guard->deviceLost();
        m_nowMs = coolDownMs;
        QTRY_VERIFY(drawsRealistic());
        m_nowMs = coolDownMs + 1000;
        m_guard->deviceLost();
        QVERIFY(!drawsRealistic());
        QCOMPARE(notices.count(), 1);
        QVERIFY(!notices.first().first().toString().isEmpty());
        m_nowMs = 10 * QcDeviceLossPolicy::repeatWindowMs;
        QTest::qWait(3 * static_cast<int>(coolDownMs));
        QVERIFY(!drawsRealistic());

        // Classic and back to realistic tries again.
        m_settings->rendering()->setMode(Mode::Classic);
        QVERIFY(!drawsRealistic());
        m_settings->rendering()->setMode(Mode::Realistic);
        QVERIFY(drawsRealistic());
    }

    void aLossInTheClassicLookChangesNothing()
    {
        m_settings->rendering()->setMode(Mode::Classic);
        m_guard->deviceLost();
        m_settings->rendering()->setMode(Mode::Realistic);
        QVERIFY(drawsRealistic());
        // The classic loss was no first realistic one: this one only cools down.
        const QSignalSpy notices(m_settings->rendering(), &QcRenderSettings::announced);
        m_guard->deviceLost();
        QVERIFY(!drawsRealistic());
        QCOMPARE(notices.count(), 0);
    }

    void choosingTheModeAgainEndsTheCoolDown()
    {
        m_guard->deviceLost();
        QVERIFY(!drawsRealistic());
        m_settings->rendering()->setMode(Mode::Classic);
        m_settings->rendering()->setMode(Mode::Realistic);
        QVERIFY(drawsRealistic());
    }
};

QTEST_MAIN(TestGraphicsGuard)
#include "test_graphics_guard.moc"
