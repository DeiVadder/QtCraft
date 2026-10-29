// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/diagnostics/QcHealthSample.h"
#include "common/protocol/QcInProcessTransport.h"
#include "fixtures/QcServerTestFixture.h"

#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

// The pause of the server (decision E-877), ticked by hand: the pause menu of the host's own player
// stops the tick counter, the time of day and the clients' clocks; a network player's wish does
// not; another player connected keeps the world running; an empty server pauses after its timeout,
// saving first, and resumes as soon as someone connects.
class TestServerPause : public QcServerTestFixture
{
    Q_OBJECT

private:
    // A network player joining the server of the fixture; logged in after a few ticks.
    std::unique_ptr<RawClient> connectGuest()
    {
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd), QcServer::ClientOrigin::Remote);
        auto guest = std::make_unique<RawClient>(std::move(clientEnd));
        guest->hello(u"Guest"_s);
        for (int i = 0; i < 10 && guest->playerList.size() < 2; ++i) {
            QcTestUtil::pump();
            ticks(1);
        }
        return guest;
    }

    void requestPause(bool paused)
    {
        m_client->connection.send(QcMsg::PauseRequest{paused});
        QcTestUtil::pump();
    }

private slots:
    void init()
    {
        m_origin = QcServer::ClientOrigin::Local;
    }

    void pauseMenuStopsTheTicks()
    {
        join();
        const quint64 before = m_server->tickCount();
        const double timeBefore = m_server->worldTime();

        requestPause(true);
        QVERIFY(m_server->pause().paused());
        ticks(40);
        QCOMPARE(m_server->tickCount(), before);
        QCOMPARE(m_server->worldTime(), timeBefore);
        // The client hears of it, and its clock of the day stands still.
        QCOMPARE(m_client->pauses, QList<bool>{true});
        QCOMPARE(m_client->timeSpeed, 0.0);
        QVERIFY(m_server->health().paused);

        requestPause(false);
        ticks(5);
        QCOMPARE(m_server->tickCount(), before + 5);
        QCOMPARE(m_client->pauses, (QList<bool>{true, false}));
        QCOMPARE(m_client->timeSpeed, 1.0);
        QVERIFY(!m_server->health().paused);
    }

    void networkPlayerCannotPause()
    {
        m_origin = QcServer::ClientOrigin::Remote;
        join();
        const quint64 before = m_server->tickCount();

        requestPause(true);
        ticks(10);
        QVERIFY(!m_server->pause().paused());
        QCOMPARE(m_server->tickCount(), before + 10);
        QVERIFY(m_client->pauses.isEmpty());
    }

    void otherPlayerKeepsTheWorldRunning()
    {
        // A world opened to the LAN: the host's pause menu does not pause while a guest plays.
        join();
        std::unique_ptr<RawClient> guest = connectGuest();
        QCOMPARE(guest->playerList.size(), 2);
        requestPause(true);
        const quint64 before = m_server->tickCount();
        ticks(10);
        QVERIFY(!m_server->pause().paused());
        QCOMPARE(m_server->tickCount(), before + 10);

        // The guest leaves: the host's menu pauses the world.
        guest->connection.close();
        QcTestUtil::pump();
        ticks(2);
        QVERIFY(m_server->pause().paused());
        QCOMPARE(m_server->pause().reason(), QcServerPause::Reason::Menu);
        const quint64 paused = m_server->tickCount();
        ticks(10);
        QCOMPARE(m_server->tickCount(), paused);

        // Someone connecting resumes it at once, before the login is done.
        std::unique_ptr<RawClient> second = connectGuest();
        QVERIFY(!m_server->pause().paused());
        QCOMPARE(second->playerList.size(), 2);
        QVERIFY(m_server->tickCount() > paused);
    }

    void emptyServerPausesAndResumesOnConnect()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QcServerConfig config;
        config.worldDirectory = dir.filePath(u"world"_s);
        config.pauseWhenEmptySeconds = 1;
        config.autosaveSeconds = 600;
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());

        // One second of the players' time is 20 ticks; the pause begins with the tick after it.
        ticks(20);
        QVERIFY(!m_server->pause().paused());
        const quint64 autosaves = m_server->autosaveCount();
        ticks(1);
        QVERIFY(m_server->pause().paused());
        QCOMPARE(m_server->pause().reason(), QcServerPause::Reason::Empty);
        QCOMPARE(m_server->autosaveCount(), autosaves + 1);
        const quint64 paused = m_server->tickCount();
        ticks(100);
        QCOMPARE(m_server->tickCount(), paused);
        QVERIFY(m_server->health().paused);

        // A login resumes it at once; the player plays.
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        m_server->addClient(std::move(serverEnd), QcServer::ClientOrigin::Remote);
        QVERIFY(!m_server->pause().paused());
        m_client = std::make_unique<RawClient>(std::move(clientEnd));
        m_client->hello(u"Raw"_s);
        QcTestUtil::pump();
        ticks(3);
        QCOMPARE(m_server->tickCount(), paused + 3);
        QCOMPARE(m_client->playerList.size(), 1);

        // Leaving starts the count again; the server rests one second later.
        m_client->connection.close();
        QcTestUtil::pump();
        ticks(22);
        QVERIFY(m_server->pause().paused());
        m_server->waitForStorage();
    }

    void menuPauseWaitsForTheView()
    {
        // The local player asked, but its view has not arrived: no pause until it has.
        std::vector<std::unique_ptr<QcServerClient>> clients;
        clients.push_back(std::make_unique<QcServerClient>());
        clients.back()->local = true;
        clients.back()->pauseRequested = true;
        clients.back()->state = QcServerClient::State::Playing;
        QcServerPause pause(0);
        bool complete = false;
        const auto view = [&complete](const QcServerClient &) { return complete; };
        QCOMPARE(pause.update(clients, 0, view), std::nullopt);
        QVERIFY(!pause.paused());
        complete = true;
        QCOMPARE(pause.update(clients, 0, view), std::optional(QcServerPause::Reason::Menu));
        // A network client's wish would not count, even with its view.
        clients.back()->local = false;
        QCOMPARE(pause.update(clients, 0, view), std::optional(QcServerPause::Reason::None));
    }

    void sprintWaitsWhilePaused()
    {
        join();
        requestPause(true);
        QVERIFY(m_server->pause().paused());
        QcTickScheduler &scheduler = m_server->tickScheduler();
        m_server->resetTickTimes();
        scheduler.startSprint(10);
        for (int i = 0; i < 5; ++i) {
            scheduler.runDue([this] { m_server->tick(); });
        }
        QCOMPARE(scheduler.sprintRemaining(), quint64(10));
        QCOMPARE(m_server->tickTimes().ticks, quint64(0));

        requestPause(false);
        QVERIFY(QTest::qWaitFor(
            [&] {
                scheduler.runDue([this] { m_server->tick(); });
                return !scheduler.sprinting();
            },
            5000));
    }

    void zeroNeverPauses()
    {
        QcServerConfig config;
        config.pauseWhenEmptySeconds = 0;
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        ticks(200);
        QVERIFY(!m_server->pause().paused());
        QCOMPARE(m_server->tickCount(), quint64(200));
    }
};

QTEST_GUILESS_MAIN(TestServerPause)
#include "test_server_pause.moc"
