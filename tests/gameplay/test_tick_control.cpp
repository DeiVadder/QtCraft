#include "QcServerTestFixture.h"
#include "QcTestUtil.h"

#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace Qt::StringLiterals;

// /tick (decision E-249): freezing, steps, the tick rate, sprints and what the clients learn of it.
class TestTickControl : public QcServerTestFixture
{
    Q_OBJECT

private:
    QString lastLine() const { return m_client->chatLines.isEmpty() ? QString() : m_client->chatLines.constLast(); }

    void sendWalkEast()
    {
        QcPlayerInput input;
        input.sequence = ++m_sequence;
        input.yaw = 90.0f;
        input.forward = 1.0f;
        m_client->connection.send(QcMsg::PlayerInput{input});
    }

    // Chat lines of the raw client the server passed on to everyone.
    qsizetype ownChatLines() const
    {
        return std::ranges::count_if(m_client->chatLines, [](const QString &line) { return line.startsWith(u"<Raw> "_s); });
    }

    void walkEast(int count)
    {
        for (int i = 0; i < count; ++i) {
            QcPlayerInput input;
            input.sequence = ++m_sequence;
            input.yaw = 90.0f;
            input.forward = 1.0f;
            m_client->connection.send(QcMsg::PlayerInput{input});
            QcTestUtil::pump();
            ticks(1);
        }
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_server.reset();
    }

    void aFrozenWorldStandsStillButPlayersMove()
    {
        join();
        const int zombie = *m_content->mobIndex(u"qtcraft:zombie"_s);
        const quint32 mob = m_server->mobs().spawn(zombie, m_client->motion.position + QcVec3{3.0, 0.0, 0.0}, 900001);
        const QcBlockPos water = m_feet.offset(-2, 0, -2);

        chat(u"/tick freeze"_s);
        QCOMPARE(lastLine(), u"The world is frozen; /tick step moves it on"_s);
        ticks(1);
        QCOMPARE(m_client->timeSpeed, 0.0);
        const double time = m_server->worldTime();
        const QcVec3 mobAt = m_server->mobs().find(mob)->motion.position;
        chat(u"/setblock %1 %2 %3 water"_s.arg(water.x).arg(water.y).arg(water.z));
        const double x = m_client->motion.position.x;
        walkEast(30);

        QCOMPARE(m_server->worldTime(), time);
        QCOMPARE(m_server->mobs().find(mob)->motion.position, mobAt);
        QCOMPARE(*m_server->world().blockAt(water.offset(1, 0, 0)), qcAirBlockId);
        QVERIFY(m_client->motion.position.x > x + 2.0);

        chat(u"/tick unfreeze"_s);
        ticks(1);
        QCOMPARE(m_client->timeSpeed, 1.0);
        ticks(40);
        QVERIFY(m_server->worldTime() > time);
        QVERIFY(m_server->liquids().plannedCount() > 0 || *m_server->world().blockAt(water.offset(1, 0, 0)) != qcAirBlockId);
    }

    void stepsRunExactlyTheGivenTicks()
    {
        join();
        chat(u"/tick step"_s);
        QCOMPARE(lastLine(), u"Steps only work while the world is frozen (/tick freeze)"_s);
        chat(u"/tick freeze"_s);
        const double time = m_server->worldTime();
        chat(u"/tick step 7"_s);
        QCOMPARE(lastLine(), u"Stepping 7 ticks"_s);
        ticks(20);
        QCOMPARE(m_server->worldTime(), time + 7 * QcPhysics::tickDuration);
        // The clients do not advance a frozen time themselves; each step sends it.
        QCOMPARE(m_client->worldTime, m_server->worldTime());

        chat(u"/tick step"_s);
        ticks(3);
        QCOMPARE(m_server->worldTime(), time + 8 * QcPhysics::tickDuration);
        for (const QString &bad : {u"/tick step 0"_s, u"/tick step 6001"_s, u"/tick step x"_s}) {
            chat(bad);
            QVERIFY2(lastLine().startsWith(u"Usage: /tick"_s), qPrintable(bad));
        }
    }

    void theRateReachesTheClients()
    {
        join();
        chat(u"/tick rate 5"_s);
        QCOMPARE(lastLine(), u"The tick rate is now 5.0 per second"_s);
        QCOMPARE(m_server->tickScheduler().currentIntervalNs(), qint64(200'000'000));
        ticks(1);
        QCOMPARE(m_client->timeSpeed, 0.25);

        // Four inputs per tick of 200 ms keep the player's pace.
        const double x = m_client->motion.position.x;
        for (int i = 0; i < 4; ++i) {
            QcPlayerInput input;
            input.sequence = ++m_sequence;
            input.yaw = 90.0f;
            input.forward = 1.0f;
            m_client->connection.send(QcMsg::PlayerInput{input});
        }
        QcTestUtil::pump();
        ticks(1);
        const double fourInputs = m_client->motion.position.x - x;
        QVERIFY2(fourInputs > 4 * 0.1, qPrintable(QString::number(fourInputs)));

        for (const QString &bad : {u"/tick rate 0"_s, u"/tick rate 101"_s, u"/tick rate fast"_s, u"/tick rate nan"_s}) {
            chat(bad);
            QCOMPARE(lastLine(), u"The tick rate must lie between 1.0 and 100.0 ticks per second"_s);
        }
        chat(u"/tick rate reset"_s);
        ticks(1);
        QCOMPARE(m_server->tickScheduler().rate(), 20.0);
        QCOMPARE(m_client->timeSpeed, 1.0);
    }

    void slowRatesKeepThePlayersPace_data()
    {
        QTest::addColumn<int>("rate");
        QTest::newRow("1 per second") << 1;
        QTest::newRow("2 per second") << 2;
    }

    void slowRatesKeepThePlayersPace()
    {
        // The client sends 20 inputs per second whatever the server's rate; a tick of 1 s takes all
        // 20 of them, not only the 10 of the normal budget (decision E-249).
        QFETCH(int, rate);
        join();
        // A flat way east, so only dropped inputs could shorten the walk.
        for (int dx = 0; dx <= 15; ++dx) {
            m_server->world().setBlock(m_feet.offset(dx, -1, 0), id("qtcraft:grass_block"));
            for (int dy = 0; dy <= 2; ++dy) {
                m_server->world().setBlock(m_feet.offset(dx, dy, 0), qcAirBlockId);
            }
        }
        chat(u"/tick rate %1"_s.arg(rate));
        ticks(1);
        const int perTick = 20 / rate;
        const double x = m_client->motion.position.x;
        for (int tick = 0; tick < 3; ++tick) {
            for (int i = 0; i < perTick; ++i) {
                sendWalkEast();
            }
            QcTestUtil::pump();
            ticks(1);
        }

        QCOMPARE(m_client->acknowledged, m_sequence);
        QVERIFY2(m_client->motion.position.x > x + 3 * perTick * 0.15, qPrintable(QString::number(m_client->motion.position.x - x)));
    }

    void chatLimitsFollowRealTimeAtEveryRate()
    {
        // Chat: 2 lines per second, bursts of 20 (decision E-091). At 1 tick per second a line per
        // second always passes; at 100 ticks per second a second of spam passes no more than at 20.
        join();
        chat(u"/tick rate 1"_s);
        ticks(1);
        qsizetype before = ownChatLines();
        for (int i = 0; i < 30; ++i) {
            chat(u"line %1"_s.arg(i));
            ticks(1);
        }
        QCOMPARE(ownChatLines() - before, 30);

        cleanup();
        join();
        chat(u"/tick rate 100"_s);
        ticks(1);
        before = ownChatLines();
        for (int tick = 0; tick < 100; ++tick) {
            for (int i = 0; i < 3; ++i) {
                m_client->connection.send(QcMsg::ChatInput{u"spam"_s});
            }
            QcTestUtil::pump();
            ticks(1);
        }
        // The burst left after the /tick line plus one second of refill.
        const qsizetype passed = ownChatLines() - before;
        QVERIFY2(passed >= 20 && passed <= 22, qPrintable(QString::number(passed)));
    }

    void aSprintKeepsTheRealTimePace()
    {
        // During a sprint, limits, autosaves and messages to the clients follow real time, not the
        // thousands of ticks per second (decision E-249).
        QTemporaryDir dir;
        QcServerConfig config;
        config.worldDirectory = dir.path();
        config.autosaveSeconds = 1;
        join(config);
        const int zombie = *m_content->mobIndex(u"qtcraft:zombie"_s);
        m_server->mobs().spawn(zombie, m_client->motion.position + QcVec3{3.0, 0.0, 0.0}, 900002, false, true);
        m_server->start();
        const quint64 autosaves = m_server->autosaveCount();
        const qsizetype chatBefore = ownChatLines();
        const int timeBefore = m_client->timeMessages;
        const int movesBefore = m_client->entityMoveMessages;
        double fastest = 0.0;

        QElapsedTimer timer;
        timer.start();
        chat(u"/tick sprint 2000"_s);
        while (!lastLine().startsWith(u"Sprint ended"_s) && timer.elapsed() < 60'000) {
            for (int i = 0; i < 3; ++i) {
                m_client->connection.send(QcMsg::ChatInput{u"spam"_s});
            }
            QCoreApplication::processEvents();
            fastest = std::max(fastest, m_client->timeSpeed);
        }
        const double seconds = double(timer.elapsed()) / 1000.0;
        QVERIFY2(lastLine().startsWith(u"Sprint ended: 2000 ticks"_s), qPrintable(lastLine()));

        // 2000 ticks are 100 s of game time; real time is what counts.
        const auto bound = [seconds](double perSecond) { return int(seconds * perSecond) + 3; };
        const QString counts = u"%1 s: %2 autosaves, %3 times, %4 moves, %5 chat lines"_s.arg(seconds).arg(m_server->autosaveCount() - autosaves)
                                   .arg(m_client->timeMessages - timeBefore).arg(m_client->entityMoveMessages - movesBefore)
                                   .arg(ownChatLines() - chatBefore);
        QVERIFY2(m_server->autosaveCount() - autosaves <= quint64(bound(1.0)), qPrintable(counts));
        QVERIFY2(m_client->timeMessages - timeBefore <= bound(20.0), qPrintable(counts));
        QVERIFY2(m_client->entityMoveMessages - movesBefore <= bound(20.0), qPrintable(counts));
        QVERIFY2(ownChatLines() - chatBefore <= 20 + bound(2.0), qPrintable(counts));
        // The clients learn how fast the day runs during the sprint.
        QVERIFY2(fastest > 1.0, qPrintable(QString::number(fastest)));
    }

    void aSprintIsSilent()
    {
        // A sprint skips game time; the sounds of its thousands of ticks would reach the clients as
        // one burst, so none are sent (decision E-249). Before and after it the crowd is heard.
        join();
        const int pig = *m_content->mobIndex(u"qtcraft:pig"_s);
        quint32 id = 900100;
        for (int x = 3; x < 8; ++x) {
            for (int z = -2; z < 2; ++z) {
                m_server->mobs().spawn(pig, m_client->motion.position + QcVec3{double(x), 0.0, double(z)}, id++, false, true);
            }
        }
        qsizetype heard = m_client->sounds.size();
        ticks(200);
        QVERIFY2(m_client->sounds.size() > heard, "the crowd is heard in normal play");

        m_server->start();
        chat(u"/tick sprint 2000"_s);
        heard = m_client->sounds.size();
        QElapsedTimer timer;
        timer.start();
        while (!lastLine().startsWith(u"Sprint ended"_s) && timer.elapsed() < 60'000) {
            QCoreApplication::processEvents();
        }
        QVERIFY2(lastLine().startsWith(u"Sprint ended: 2000 ticks"_s), qPrintable(lastLine()));
        QCOMPARE(m_client->sounds.size(), heard);

        QTRY_VERIFY_WITH_TIMEOUT(m_client->sounds.size() > heard, 20'000);
    }

    void aSprintRunsAheadAndReportsTheEnd()
    {
        join();
        chat(u"/tick sprint 1d"_s);
        QCOMPARE(lastLine(), u"Sprinting 12000 ticks (600.0 s of game time); /tick sprint stop ends it"_s);
        QCOMPARE(m_server->tickScheduler().sprintRemaining(), quint64(12000));
        chat(u"/tick sprint stop"_s);
        QVERIFY(lastLine().startsWith(u"Sprint ended: 0 ticks"_s));
        chat(u"/tick sprint stop"_s);
        QCOMPARE(lastLine(), u"No sprint is running"_s);
        for (const QString &bad : {u"/tick sprint 0"_s, u"/tick sprint 2000d"_s, u"/tick sprint soon"_s}) {
            chat(bad);
            QCOMPARE(lastLine(), u"A sprint runs 1 to 864000 ticks"_s);
        }
        chat(u"/tick freeze"_s);
        chat(u"/tick sprint 10"_s);
        QCOMPARE(lastLine(), u"The world is frozen; unfreeze it first or use /tick step"_s);
        chat(u"/tick unfreeze"_s);

        // Running for real: 400 ticks take 20 s at the normal pace, a sprint much less.
        m_server->start();
        const quint64 before = m_server->tickCount();
        const double time = m_server->worldTime();
        chat(u"/tick sprint 400"_s);
        QTRY_VERIFY_WITH_TIMEOUT(lastLine().startsWith(u"Sprint ended: 400 ticks"_s), 10000);
        QVERIFY(m_server->tickCount() >= before + 400);
        QVERIFY(m_server->worldTime() >= time + 400 * QcPhysics::tickDuration - 1e-6);
        // No backlog: the normal pace goes on without skipped ticks.
        QTest::qWait(200);
        QCOMPARE(m_server->tickTimes().skipped, quint64(0));
    }

    void freezingEndsASprint()
    {
        join();
        chat(u"/tick sprint 1000"_s);
        QVERIFY(m_server->tickScheduler().sprinting());
        chat(u"/tick freeze"_s);
        QVERIFY(!m_server->tickScheduler().sprinting());
        QVERIFY(m_server->tickControl().frozen());
        QVERIFY(m_client->chatLines.at(m_client->chatLines.size() - 2).startsWith(u"Sprint ended: 0 ticks"_s));
        QCOMPARE(lastLine(), u"The world is frozen; /tick step moves it on"_s);

        // Frozen means frozen: no tick of a sprint simulates.
        const double time = m_server->worldTime();
        m_server->start();
        QTest::qWait(300);
        QCOMPARE(m_server->worldTime(), time);
    }

    void theQueryNamesRateFreezeAndPhases()
    {
        join();
        ticks(3);
        chat(u"/tick query"_s);
        QVERIFY(m_client->chatLines.at(m_client->chatLines.size() - 2).startsWith(u"Tick rate 20.0 per second (measured "_s));
        QCOMPARE(m_client->chatKeys.at(m_client->chatKeys.size() - 2).key, u"Tick rate %1 per second (measured %2), running"_s);
        QVERIFY(lastLine().contains(u"phases (ms): players "_s));
        chat(u"/tick freeze"_s);
        chat(u"/tick step 3"_s);
        chat(u"/tick query"_s);
        QVERIFY(m_client->chatLines.at(m_client->chatLines.size() - 2).endsWith(u"frozen, steps left: 3"_s));
        chat(u"/tick"_s);
        QVERIFY(lastLine().startsWith(u"Usage: /tick"_s));

        // Only operators control time.
        cleanup();
        join(notOperator());
        chat(u"/tick freeze"_s);
        QCOMPARE(lastLine(), u"Only operators can use /tick"_s);
        QVERIFY(!m_server->tickControl().frozen());
    }
};

QTEST_GUILESS_MAIN(TestTickControl)
#include "test_tick_control.moc"
