// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fixtures/QcTestUtil.h"
#include "server/core/QcServer.h"
#include "server/tick/QcClientSyncPace.h"
#include "server/tick/QcServerClock.h"
#include "server/tick/QcTickPhaseTimer.h"
#include "server/tick/QcTickSystems.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"

#include <QTest>

#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

// A clock the test moves by hand.
qint64 fakeNow = 0;
qint64 fakeClock()
{
    return fakeNow;
}

constexpr qint64 ms = 1'000'000;

using Due = QcClientSyncPace::Due;

// Advances `clock` by one tick of `realNs` and asks `pace` what is due after it.
Due tickAndAsk(QcServerClock &clock, QcClientSyncPace &pace, qint64 realNs, bool sprinting)
{
    clock.advance(realNs);
    return pace.next(clock, sprinting);
}

} // namespace

class TestTickSystems : public QObject
{
    Q_OBJECT

private slots:
    void systemsRunInTheOrderTheyWereAdded()
    {
        using When = QcTickSystems::When;
        std::string calls;
        QcTickSystems systems;
        systems.add(QcTickPhase::Players, When::Always, [&calls] { calls += 'p'; });
        systems.add(QcTickPhase::Liquids, When::Simulating, [&calls] { calls += 'l'; });
        systems.add(QcTickPhase::ClientSync, When::Always, [&calls] { calls += 'c'; });
        systems.add(QcTickPhase::Streaming, When::Always, [&calls] { calls += 's'; });
        systems.add(QcTickPhase::ClientSync, When::Simulating, [&calls] { calls += 'C'; });
        QCOMPARE(systems.systems().size(), std::size_t(5));

        QcTickPhaseTimer timer(&fakeClock);
        timer.beginTick();
        systems.run(true, timer);
        QCOMPARE(calls, std::string("plcsC"));

        // A tick that does not simulate leaves out the systems of the simulation only.
        calls.clear();
        systems.run(false, timer);
        QCOMPARE(calls, std::string("pcs"));
    }

    void eachSystemsTimeGoesToItsPhase()
    {
        using When = QcTickSystems::When;
        QcTickSystems systems;
        systems.add(QcTickPhase::Mobs, When::Simulating, [] { fakeNow += 3 * ms; });
        systems.add(QcTickPhase::Items, When::Simulating, [] { fakeNow += 2 * ms; });
        // A system that laps another phase inside keeps the rest for its own.
        QcTickPhaseTimer timer(&fakeClock);
        systems.add(QcTickPhase::ClientSync, When::Always, [&timer] {
            fakeNow += 1 * ms;
            timer.lap(QcTickPhase::ClientSync);
            fakeNow += 4 * ms;
            timer.lap(QcTickPhase::Streaming);
            fakeNow += 5 * ms;
        });
        systems.add(QcTickPhase::Mobs, When::Simulating, [] { fakeNow += 7 * ms; });

        fakeNow = 0;
        timer.beginTick();
        systems.run(true, timer);
        timer.endTick(0);
        const QcTickPhaseTimer::Durations &tick = timer.lastTick();
        QCOMPARE(tick[std::size_t(QcTickPhase::Mobs)], 10 * ms);
        QCOMPARE(tick[std::size_t(QcTickPhase::Items)], 2 * ms);
        QCOMPARE(tick[std::size_t(QcTickPhase::ClientSync)], 6 * ms);
        QCOMPARE(tick[std::size_t(QcTickPhase::Streaming)], 4 * ms);

        timer.beginTick();
        systems.run(false, timer);
        timer.endTick(0);
        QCOMPARE(timer.lastTick()[std::size_t(QcTickPhase::Mobs)], qint64(0));
        QCOMPARE(timer.lastTick()[std::size_t(QcTickPhase::ClientSync)], 6 * ms);
    }

    void serverRegistersItsSystemsInTheTickOrder()
    {
        using enum QcTickPhase;
        constexpr QcTickSystems::When always = QcTickSystems::When::Always;
        constexpr QcTickSystems::When simulating = QcTickSystems::When::Simulating;
        const std::vector<std::pair<QcTickPhase, QcTickSystems::When>> expected{
            {Players, always},        {Saving, always},       {ChunkLoading, always},  {BlockUpdates, simulating},
            {Signal, simulating},     {Liquids, simulating},  {Fire, simulating},      {Weather, simulating},
            {Mobs, simulating},       {Projectiles, simulating}, {Items, simulating},  {Plants, simulating},
            {FallingBlocks, simulating}, {Explosions, simulating}, {Minecarts, simulating}, {Machines, simulating},
            // Chunk loading notes the tick, held maps are revealed, the time goes out in a step of a
            // frozen world, the clients, the adventure guide (decision E-923), the admin channel
            // (decision E-848).
            {ClientSync, always},     {ClientSync, simulating}, {ClientSync, simulating}, {ClientSync, always},
            {ClientSync, always},     {ClientSync, always},
            {Streaming, always},
        };

        const std::shared_ptr<const QcContentRegistry> content = QcTestUtil::loadContent();
        const QcServer server(QcServerConfig{}, content, std::make_unique<QcTestTerrainGenerator>(content));
        std::vector<std::pair<QcTickPhase, QcTickSystems::When>> registered;
        for (const QcTickSystems::System &system : server.tickSystems().systems()) {
            QVERIFY(system.run);
            registered.emplace_back(system.phase, system.when);
        }
        QCOMPARE(registered.size(), expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i) {
            QVERIFY2(registered[i] == expected[i], qPrintable(QStringLiteral("system %1: phase %2").arg(i).arg(QString::fromLatin1(QcTickPhases::id(registered[i].first)))));
        }
    }

    void worldEveryTickAndTimeEverySecond()
    {
        QcServerClock clock;
        QcClientSyncPace pace;
        // 50 ms ticks: the world goes out with each, the time when a second of players' time is full.
        for (int tick = 1; tick <= 40; ++tick) {
            const Due due = tickAndAsk(clock, pace, 50 * ms, false);
            QVERIFY(due.world);
            QCOMPARE(due.time, tick % 20 == 0);
        }

        // A slow tick rate: each 200 ms tick crosses a second only every fifth time.
        int times = 0;
        for (int tick = 0; tick < 10; ++tick) {
            times += tickAndAsk(clock, pace, 200 * ms, false).time ? 1 : 0;
        }
        QCOMPARE(times, 2);
    }

    void sprintSyncsOncePerInterval()
    {
        QcServerClock clock;
        QcClientSyncPace pace;
        // 10 ms sprint ticks: the world and the time go out together at 50, 100, ... ms.
        for (int tick = 1; tick <= 20; ++tick) {
            const Due due = tickAndAsk(clock, pace, 10 * ms, true);
            QCOMPARE(due.world, tick % 5 == 0);
            QCOMPARE(due.time, due.world);
        }

        // Just under the interval since the last sync (200 ms) nothing goes out, at it both go.
        QVERIFY(!tickAndAsk(clock, pace, 49 * ms, true).world);
        const Due boundary = tickAndAsk(clock, pace, 1 * ms, true);
        QVERIFY(boundary.world);
        QVERIFY(boundary.time);
    }

    void sprintStartingAndEndingSwitchesThePace()
    {
        QcServerClock clock;
        QcClientSyncPace pace;
        // A sprint syncs at 50 ms.
        for (int tick = 0; tick < 5; ++tick) {
            std::ignore = tickAndAsk(clock, pace, 10 * ms, true);
        }
        QVERIFY(!tickAndAsk(clock, pace, 10 * ms, true).world);

        // After it ends the world goes out with every tick and the time by the second again (60 ms
        // to 960 ms crosses no second, 1010 ms does).
        for (int tick = 0; tick < 18; ++tick) {
            const Due due = tickAndAsk(clock, pace, 50 * ms, false);
            QVERIFY(due.world);
            QVERIFY(!due.time);
        }
        QVERIFY(tickAndAsk(clock, pace, 50 * ms, false).time);

        // A new sprint long after the last sprint sync sends at once, then waits for the interval.
        const Due first = tickAndAsk(clock, pace, 10 * ms, true);
        QVERIFY(first.world);
        QVERIFY(first.time);
        QVERIFY(!tickAndAsk(clock, pace, 10 * ms, true).world);
    }

    void messagesAndEmptySystemsAreRefused()
    {
        QcTickSystems systems;
        QTest::ignoreMessage(QtWarningMsg, "Tick system refused: phase messages is not part of the tick");
        systems.add(QcTickPhase::Messages, QcTickSystems::When::Always, [] {});
        QTest::ignoreMessage(QtWarningMsg, "Tick system refused: phase fire has no entry point");
        systems.add(QcTickPhase::Fire, QcTickSystems::When::Simulating, {});
        QVERIFY(systems.systems().empty());
    }
};

QTEST_GUILESS_MAIN(TestTickSystems)
#include "test_tick_systems.moc"
