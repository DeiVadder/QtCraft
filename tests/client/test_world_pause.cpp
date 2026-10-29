// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcHandbookController.h"
#include "app/controllers/QcScreenState.h"
#include "app/controllers/QcSettingsController.h"
#include "app/controllers/QcWorldLibraryController.h"
#include "app/controllers/QcWorldPause.h"
#include "app/models/QcItemIcons.h"
#include "app/session/QcSessionParts.h"
#include "audio/QcAudioEngine.h"
#include "clientcore/meshing/QcMeshScheduler.h"
#include "clientcore/session/QcClientSession.h"
#include "clientcore/session/QcSnapshotClock.h"
#include "clientcore/textures/QcAnimationClock.h"
#include "QcServerTestFixture.h"
#include "worlds/QcLocalServerHost.h"

#include <QSignalSpy>
#include <QTest>

#include <tuple>

using namespace Qt::StringLiterals;
using Overlay = QcScreenState::Overlay;

// The pause of the game (decision E-877) from the screens to the server, ticked by hand: the pause
// menu stops the tick counter; the handbook, settings and other screens opened from the pause menu
// keep the pause until the player is back in the game; the handbook, the inventory and the debug
// options opened from the game do not pause. The wish of QcWorldPause goes to the server as the
// game's session sends it (QcMsg::PauseRequest of the host's own player).
class TestWorldPause : public QcServerTestFixture
{
    Q_OBJECT

private:
    QcSessionParts m_parts;
    QcAudioEngine m_audio;
    QcWorldLibraryController m_library;
    std::unique_ptr<QcSettingsController> m_settings;
    std::unique_ptr<QcScreenState> m_screens;
    std::unique_ptr<QcWorldPause> m_pause;
    quint64 m_lastTick = 0;
    static constexpr int timeoutMs = 20000;

    // Ticks the server by hand; true when its counter moved.
    bool ticking()
    {
        ticks(5);
        const bool moved = m_server->tickCount() > m_lastTick;
        m_lastTick = m_server->tickCount();
        return moved;
    }

private slots:
    void init()
    {
        m_origin = QcServer::ClientOrigin::Local;
        join();
        m_settings = std::make_unique<QcSettingsController>(m_parts, m_library, &m_audio);
        m_screens = std::make_unique<QcScreenState>(m_parts);
        m_pause = std::make_unique<QcWorldPause>(m_parts, *m_screens, *m_settings);
        connect(m_pause.get(), &QcWorldPause::wantedChanged, this, [this](bool wanted) {
            m_client->connection.send(QcMsg::PauseRequest{wanted});
            QcTestUtil::pump();
        });
        m_screens->setPhase(QcScreenState::Phase::Playing);
        m_lastTick = m_server->tickCount();
    }

    void cleanup()
    {
        m_parts.session.reset();
        if (m_parts.server) {
            std::ignore = m_parts.server->shutdown();
            m_parts.server.reset();
        }
        m_pause.reset();
        m_screens.reset();
        m_settings.reset();
    }

    void pauseMenuStopsTheTicks()
    {
        QVERIFY(ticking());
        m_screens->setOverlay(Overlay::Pause);
        QVERIFY(m_pause->wanted());
        QVERIFY(!ticking());
        m_screens->setOverlay(Overlay::None);
        QVERIFY(!m_pause->wanted());
        QVERIFY(ticking());
    }

    void pauseBelongsToTheMenuNotToTheScreen()
    {
        QcItemIcons icons;
        QcHandbookController handbook(m_parts, *m_settings, m_library, icons);
        QSignalSpy wishes(m_pause.get(), &QcWorldPause::wantedChanged);

        // Pause menu, handbook from there, back to the pause menu, settings, back: paused throughout.
        m_screens->setOverlay(Overlay::Pause);
        QVERIFY(!ticking());
        handbook.open();
        QVERIFY(handbook.isOpen());
        QVERIFY(!ticking());
        handbook.close();
        QVERIFY(!ticking());
        m_screens->setOverlay(Overlay::Settings);
        QVERIFY(!ticking());
        m_screens->setOverlay(Overlay::Character);
        QVERIFY(!ticking());
        m_screens->setOverlay(Overlay::Pause);
        QVERIFY(!ticking());
        // Only the step back into the game resumes it; the wish changed twice in all.
        m_screens->setOverlay(Overlay::None);
        QVERIFY(ticking());
        QCOMPARE(wishes.count(), 2);
    }

    void screensOpenedFromTheGameKeepTicking()
    {
        QcItemIcons icons;
        QcHandbookController handbook(m_parts, *m_settings, m_library, icons);
        handbook.open();
        QVERIFY(ticking());
        handbook.close();
        for (const Overlay overlay : {Overlay::Inventory, Overlay::Chest, Overlay::Chat, Overlay::Debug}) {
            m_screens->setOverlay(overlay);
            QVERIFY2(!m_pause->wanted(), qPrintable(QcScreenState::overlayName(overlay)));
            QVERIFY2(ticking(), qPrintable(QcScreenState::overlayName(overlay)));
        }
    }

    void focusLossPausesOnlyWhenChosen()
    {
        // Off (the default): an inactive window keeps playing.
        QVERIFY(!m_settings->pauseOnFocusLoss());
        m_pause->setWindowActive(false);
        QCOMPARE(m_screens->overlay(), Overlay::None);
        QVERIFY(!m_pause->wanted());
        m_pause->setWindowActive(true);

        // On: the wish follows the window; without a hosted world no pause menu opens.
        m_settings->setPauseOnFocusLoss(true);
        m_pause->setWindowActive(false);
        QVERIFY(m_pause->wanted());
        QVERIFY(!ticking());
        m_pause->setWindowActive(true);
        QVERIFY(!m_pause->wanted());
        QVERIFY(ticking());
    }

    void hostedWorldPausesThroughTheSession()
    {
        // The game's own path: QcWorldPause asks the session of the hosted world, the server pauses,
        // music and ambience fall silent; a second player keeps it running (hint); quitting while
        // paused gives the sounds back.
        QcServerConfig config;
        config.viewDistance = 1;
        m_parts.server = std::make_unique<QcLocalServerHost>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        m_parts.session = std::make_unique<QcClientSession>(m_parts.server->connectClient(), u"Host"_s);
        m_pause->sessionStarted();
        m_parts.session->start();
        QTRY_VERIFY_WITH_TIMEOUT(m_parts.session->state() == QcClientSession::State::Playing, timeoutMs);

        // The animated textures run with the server's speed (decision E-878).
        QcAnimationClock animations;
        QTRY_COMPARE_WITH_TIMEOUT(m_parts.session->serverSpeed(), 1.0, timeoutMs);
        animations.advance(0.5, m_parts.session->serverSpeed());
        QCOMPARE(animations.ticks(), 10.0);

        m_screens->setOverlay(Overlay::Pause);
        QTRY_VERIFY_WITH_TIMEOUT(m_pause->paused(), timeoutMs);
        QVERIFY(m_settings->audio()->worldPaused());
        QCOMPARE(m_pause->hint(), QcWorldPause::Hint::None);
        // Paused they stand where they were ...
        QTRY_VERIFY_WITH_TIMEOUT(m_parts.session->serverPaused(), timeoutMs);
        QCOMPARE(m_parts.session->serverSpeed(), 0.0);
        animations.advance(2.0, m_parts.session->serverSpeed());
        QCOMPARE(animations.ticks(), 10.0);
        m_screens->setOverlay(Overlay::None);
        QTRY_VERIFY_WITH_TIMEOUT(!m_pause->paused(), timeoutMs);
        // ... and go on from there.
        QTRY_COMPARE_WITH_TIMEOUT(m_parts.session->serverSpeed(), 1.0, timeoutMs);
        animations.advance(0.05, m_parts.session->serverSpeed());
        QCOMPARE(animations.ticks(), 11.0);
        QVERIFY(!m_settings->audio()->worldPaused());

        // Focus loss with the setting opens the pause menu while the host is alone.
        m_settings->setPauseOnFocusLoss(true);
        m_pause->setWindowActive(false);
        QCOMPARE(m_screens->overlay(), Overlay::Pause);
        QTRY_VERIFY_WITH_TIMEOUT(m_pause->paused(), timeoutMs);
        m_pause->setWindowActive(true);
        m_settings->setPauseOnFocusLoss(false);

        // A second player resumes it, and the menu says why.
        QcRawClient guest(m_parts.server->connectClient());
        guest.hello(u"Guest"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!m_pause->paused() && m_parts.session->players().size() == 2, timeoutMs);
        QCOMPARE(m_pause->hint(), QcWorldPause::Hint::OthersConnected);
        // With the guest there, focus loss does not open the menu.
        m_screens->setOverlay(Overlay::None);
        m_settings->setPauseOnFocusLoss(true);
        m_pause->setWindowActive(false);
        QCOMPARE(m_screens->overlay(), Overlay::None);
        m_pause->setWindowActive(true);
        guest.connection.close();
        m_screens->setOverlay(Overlay::Pause);
        QTRY_VERIFY_WITH_TIMEOUT(m_pause->paused(), timeoutMs);

        // Quitting while paused gives the sounds back.
        m_parts.session.reset();
        QVERIFY(m_parts.server->shutdown().has_value());
        m_parts.server.reset();
        m_screens->setPhase(QcScreenState::Phase::Menu);
        QVERIFY(!m_settings->audio()->worldPaused());
    }

    void joinedServerIsNotAsked()
    {
        // A session without a hosted server (a joined one): the wish stays here, the hint says why.
        QcServerConfig config;
        config.viewDistance = 1;
        QcLocalServerHost elsewhere(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        m_parts.session = std::make_unique<QcClientSession>(elsewhere.connectClient(), u"Visitor"_s);
        m_pause->sessionStarted();
        m_parts.session->start();
        QTRY_VERIFY_WITH_TIMEOUT(m_parts.session->state() == QcClientSession::State::Playing, timeoutMs);
        m_screens->setOverlay(Overlay::Pause);
        QCOMPARE(m_pause->hint(), QcWorldPause::Hint::SharedServer);
        QTest::qWait(200);
        QVERIFY(!m_parts.session->serverPaused());
        m_parts.session.reset();
        QVERIFY(elsewhere.shutdown().has_value());
    }

    void snapshotClockRestartsOnResume()
    {
        QcSnapshotClock clock;
        clock.observe(1000, 1100);
        clock.observe(1050, 1150);
        QVERIFY(clock.advance(1150).has_value());
        // The server's time stood while ours went on 5 s: a fresh history, no stale lag.
        clock.restart();
        QCOMPARE(clock.arrivalCount(), std::size_t{0});
        QVERIFY(!clock.advance(6150).has_value());
        clock.observe(1100, 6150);
        const std::optional<double> shown = clock.advance(6150);
        QVERIFY(shown.has_value());
        QCOMPARE(*shown, 1100.0 - clock.delayMs());
    }

    void noPauseOutsideTheWorld()
    {
        m_screens->setOverlay(Overlay::Pause);
        QVERIFY(m_pause->wanted());
        m_screens->setPhase(QcScreenState::Phase::Menu);
        QVERIFY(!m_pause->wanted());
        QCOMPARE(m_pause->hint(), QcWorldPause::Hint::None);
        QVERIFY(!m_pause->paused());
    }
};

QTEST_MAIN(TestWorldPause)
#include "test_world_pause.moc"
