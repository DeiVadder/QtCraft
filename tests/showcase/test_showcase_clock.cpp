// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/light/QcSky.h"
#include "clientcore/session/QcClientSession.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcInProcessTransport.h"
#include "fixtures/QcTestUtil.h"
#include "server/core/QcServer.h"
#include "showcase/QcShowcaseTestUtil.h"

#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;

// The clocks of a client in the showcase world (decision E-968), server and session ticked by hand:
// without the daylight cycle the time of day stands while the clouds drift with the animation
// clock; /gamerule daylightCycle true starts the client's time of day again; the host's pause
// stops the clouds.
class TestShowcaseClock : public QObject
{
    Q_OBJECT

    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    // One server tick and one client tick, each with its messages delivered.
    static void step(QcServer &server, QcClientSession &session, int count = 1)
    {
        for (int i = 0; i < count; ++i) {
            server.tick();
            QcTestUtil::pump();
            session.tick();
            QcTestUtil::pump();
        }
    }

    // How far the clouds a frame shows at the end of the latest client tick have drifted (x).
    static double cloudDrift(const QcClientSession &session)
    {
        const QcWorldMirror &mirror = session.worldMirror();
        return QcSky::withCloudDrift(QcSky::at(mirror.timeAt(1.0)), mirror.animationSeconds(1.0)).cloudOffsetX;
    }

    // Steps until `done` or the limit of steps; returns whether it got there.
    template<typename Done>
    static bool stepUntil(QcServer &server, QcClientSession &session, Done done)
    {
        for (int i = 0; i < 400 && !done(); ++i) {
            step(server, session);
        }
        return done();
    }

private slots:
    void cloudsDriftWhileTheTimeOfDayStandsAndTheGameruleRestartsIt()
    {
        QTemporaryDir directory;
        const std::unique_ptr<QcServer> server = QcShowcaseTestUtil::makeServer(m_content, directory.path(), u"showcase"_s, 0);
        QVERIFY(server->openWorld());
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        server->addClient(std::move(serverEnd), QcServer::ClientOrigin::Local);
        QcClientSession session(std::move(clientEnd), u"Viewer"_s);
        session.start(QcClientSession::TickMode::Manual);
        QVERIFY(stepUntil(*server, session, [&session] { return session.state() == QcClientSession::State::Playing && session.worldMirror().world(); }));
        step(*server, session, 5);

        // The showcase's time of day stands (its rule daylightCycle is false) ...
        const double standing = session.worldMirror().worldTime();
        const double drift = cloudDrift(session);
        step(*server, session, 20);
        QCOMPARE(session.worldMirror().worldTime(), standing);
        QCOMPARE(session.worldMirror().timeAt(0.5), standing);
        // ... while the clouds drift a second's way in 20 ticks.
        QVERIFY2(std::abs(cloudDrift(session) - drift - QcSky::cloudSpeed) < 1e-9, qPrintable(QString::number(cloudDrift(session) - drift)));

        // The game rule starts the client's time of day again, at the server's pace.
        session.sendChat(u"/gamerule daylightCycle true"_s);
        QVERIFY(stepUntil(*server, session, [&session, standing] { return session.worldMirror().worldTime() > standing; }));
        const double running = session.worldMirror().worldTime();
        step(*server, session, 20);
        QVERIFY2(std::abs(session.worldMirror().worldTime() - running - 1.0) < 1e-6, qPrintable(QString::number(session.worldMirror().worldTime() - running)));
        QVERIFY(std::abs(server->worldTime() - session.worldMirror().worldTime()) < 0.5);

        // The host's pause stops the clouds (and the time of day); resumed they go on.
        session.requestPause(true);
        QVERIFY(stepUntil(*server, session, [&session] { return session.serverPaused() && session.worldMirror().timeSpeed() == 0.0; }));
        step(*server, session);
        const double paused = cloudDrift(session);
        const double pausedTime = session.worldMirror().worldTime();
        step(*server, session, 20);
        QCOMPARE(cloudDrift(session), paused);
        QCOMPARE(session.worldMirror().worldTime(), pausedTime);
        session.requestPause(false);
        QVERIFY(stepUntil(*server, session, [&session] { return !session.serverPaused() && session.worldMirror().timeSpeed() == 1.0; }));
        step(*server, session, 2);
        QVERIFY(cloudDrift(session) > paused);

        // Switched off again, the time stands once more and the clouds still drift.
        session.sendChat(u"/gamerule daylightCycle false"_s);
        QVERIFY(stepUntil(*server, session, [&session] { return session.worldMirror().timeAt(0.5) == session.worldMirror().worldTime(); }));
        const double stopped = session.worldMirror().worldTime();
        const double before = cloudDrift(session);
        step(*server, session, 20);
        QCOMPARE(session.worldMirror().worldTime(), stopped);
        QVERIFY(cloudDrift(session) > before);

        session.disconnectFromServer();
        QcTestUtil::pump();
        QVERIFY(server->stop());
    }
};

QTEST_MAIN(TestShowcaseClock)
#include "test_showcase_clock.moc"
