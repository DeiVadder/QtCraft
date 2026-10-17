#include "common/core/QcPhaseWindow.h"
#include "QcServerTestFixture.h"
#include "server/mobs/QcMobSteering.h"
#include "server/tick/QcTickPhaseTimer.h"

#include <QElapsedTimer>

#include <algorithm>
#include <array>
#include <numeric>

using namespace Qt::StringLiterals;

namespace {

// A clock the test moves by hand.
qint64 fakeNow = 0;
qint64 fakeClock()
{
    return fakeNow;
}

constexpr qint64 ms = 1'000'000;

} // namespace

class TestTickPhases : public QcServerTestFixture
{
    Q_OBJECT

private slots:
    void windowGivesMeanAndMaxOfTheLatestSamples()
    {
        QcPhaseWindow window(2, 4);
        QCOMPARE(window.mean(0), 0.0);
        QCOMPARE(window.max(1), 0.0);

        for (const float value : {1.0f, 2.0f, 3.0f, 4.0f}) {
            const std::array<float, 2> sample{value, 10.0f * value};
            window.add(sample);
        }
        QCOMPARE(window.size(), 4);
        QCOMPARE(window.mean(0), 2.5);
        QCOMPARE(window.max(1), 40.0);

        // The fifth sample pushes out the first: 2, 3, 4, 9.
        const std::array<float, 2> late{9.0f, 0.0f};
        window.add(late);
        QCOMPARE(window.size(), 4);
        QCOMPARE(window.mean(0), 4.5);
        QCOMPARE(window.max(0), 9.0);
        QCOMPARE(window.mean(1), 22.5);
        QCOMPARE(window.latest(0), 9.0f);
        QCOMPARE(window.history(0, 3), (QList<float>{3.0f, 4.0f, 9.0f}));
        QCOMPARE(window.history(0, 10), (QList<float>{2.0f, 3.0f, 4.0f, 9.0f}));

        // Missing values count as 0, unknown series give 0.
        const std::array<float, 1> short_{7.0f};
        window.add(short_);
        QCOMPARE(window.latest(1), 0.0f);
        QCOMPARE(window.mean(5), 0.0);

        window.clear();
        QCOMPARE(window.size(), 0);
        QCOMPARE(window.mean(0), 0.0);
    }

    void lapsSplitTheTickIntoPhases()
    {
        fakeNow = 1000 * ms;
        QcTickPhaseTimer timer(&fakeClock);

        // Messages between ticks count for the next tick but not for its duration.
        timer.addMessageTime(3 * ms);
        timer.beginTick();
        fakeNow += 2 * ms;
        timer.lap(QcTickPhase::Mobs);
        fakeNow += 5 * ms;
        timer.lap(QcTickPhase::Streaming);
        fakeNow += 1 * ms;
        timer.lap(QcTickPhase::Mobs);
        timer.endTick(4 * ms);

        const QcTickPhaseTimer::Durations &tick = timer.lastTick();
        QCOMPARE(tick[std::size_t(QcTickPhase::Mobs)], 3 * ms);
        QCOMPARE(tick[std::size_t(QcTickPhase::Streaming)], 5 * ms);
        QCOMPARE(tick[std::size_t(QcTickPhase::Messages)], 3 * ms);
        QCOMPARE(tick[std::size_t(QcTickPhase::Liquids)], qint64(0));

        const QcPhaseWindow &window = timer.window();
        QCOMPARE(window.latest(QcTickPhaseTimer::durationSeries), 8.0f);
        QCOMPARE(window.latest(QcTickPhaseTimer::lateSeries), 4.0f);
        QCOMPARE(window.latest(int(QcTickPhase::Messages)), 3.0f);

        // The next tick starts its phases from zero; the message time was used up.
        timer.beginTick();
        fakeNow += 1 * ms;
        timer.lap(QcTickPhase::Saving);
        timer.endTick(-2 * ms);
        QCOMPARE(timer.lastTick()[std::size_t(QcTickPhase::Mobs)], qint64(0));
        QCOMPARE(timer.lastTick()[std::size_t(QcTickPhase::Messages)], qint64(0));
        QCOMPARE(timer.window().latest(QcTickPhaseTimer::lateSeries), 0.0f);
        QCOMPARE(timer.window().mean(QcTickPhaseTimer::durationSeries), 4.5);
    }

    void serverPhasesAddUpToTheTick()
    {
        join();

        // Phases are laps of one clock: together they are the tick, measured from outside.
        QElapsedTimer outside;
        qint64 measuredNs = 0;
        qint64 phasesNs = 0;
        for (int i = 0; i < 40; ++i) {
            outside.start();
            m_server->tick();
            measuredNs += outside.nsecsElapsed();
            const QcTickPhaseTimer::Durations &tick = m_server->tickPhases().lastTick();
            phasesNs += std::accumulate(tick.begin(), tick.begin() + QcTickPhases::inTick, qint64(0));
            pump();
        }

        QVERIFY2(phasesNs <= measuredNs, qPrintable(u"phases %1 ns, ticks %2 ns"_s.arg(phasesNs).arg(measuredNs)));
        // Outside the laps: the tick counter, the clock reads themselves and the window.
        QVERIFY2(phasesNs >= measuredNs * 9 / 10 - qint64(40) * 20'000, qPrintable(u"phases %1 ns, ticks %2 ns"_s.arg(phasesNs).arg(measuredNs)));

        // The streaming of the joined player's chunks and the handling of its messages are seen.
        const QcPhaseWindow &window = m_server->tickPhases().window();
        // Two ticks of join() and the forty above.
        QCOMPARE(window.size(), 42);
        QVERIFY(window.max(int(QcTickPhase::Streaming)) > 0.0);
        QVERIFY(window.max(int(QcTickPhase::ClientSync)) > 0.0);
        QVERIFY(window.mean(QcTickPhaseTimer::durationSeries) > 0.0);
    }

    void benchmarkSummaryHasThePhases()
    {
        join();
        m_server->resetTickTimes();
        m_client->connection.send(QcMsg::ChatInput{u"/help"_s});
        pump();
        ticks(10);

        const QcTickStats::Summary summary = m_server->tickTimes();
        QVERIFY(summary.phaseAverageMs[std::size_t(QcTickPhase::Streaming)] > 0.0);
        QVERIFY(summary.phaseWorstMs[std::size_t(QcTickPhase::Streaming)] >= summary.phaseAverageMs[std::size_t(QcTickPhase::Streaming)]);
        QVERIFY(summary.phaseAverageMs[std::size_t(QcTickPhase::Messages)] > 0.0);

        m_server->resetTickTimes();
        QCOMPARE(m_server->tickTimes().phaseAverageMs[std::size_t(QcTickPhase::Streaming)], 0.0);
    }

    void debugDataComesTwiceASecondWhileSubscribed()
    {
        m_origin = QcServer::ClientOrigin::Local;
        join();
        ticks(40);
        QVERIFY(m_client->debugMessages.isEmpty());

        // The first message comes with the next tick, then one every ten ticks.
        m_client->connection.send(QcMsg::DebugSubscribe{QcMsg::DebugSubscribe::stats});
        pump();
        ticks(1);
        QCOMPARE(m_client->debugMessages.size(), 1);
        ticks(40);
        QCOMPARE(m_client->debugMessages.size(), 5);

        const QcMsg::ServerDebug &last = m_client->debugMessages.constLast();
        QCOMPARE(last.tick, m_server->tickCount());
        QCOMPARE(last.players, 1u);
        QCOMPARE(last.loadedChunks, quint32(m_server->world().loadedChunkCount()));
        QCOMPARE(last.recentTickMs.size(), 10);
        QVERIFY(last.tickMeanMs > 0.0f);
        QVERIFY(last.tickMaxMs >= last.tickMeanMs);
        QCOMPARE(last.worldType, u"terrain"_s);
        // The test terrain has no biomes; the field passes on what the generator says.
        const QcVec3 feet = m_client->motion.position;
        QCOMPARE(last.biome, m_server->world().generator().biomeAt(int(std::floor(feet.x)), int(std::floor(feet.z))));
        // Without the flag no goals are sent.
        QVERIFY(last.goals.isEmpty());

        m_client->connection.send(QcMsg::DebugSubscribe{0});
        pump();
        ticks(30);
        QCOMPARE(m_client->debugMessages.size(), 5);
    }

    void goalsShowWhomCreaturesChase()
    {
        m_origin = QcServer::ClientOrigin::Local;
        join();
        const std::optional<int> zombie = m_content->mobIndex(u"qtcraft:zombie"_s);
        QVERIFY(zombie.has_value());
        const QcVec3 player = m_client->motion.position;
        const quint32 chaser = m_server->mobs().spawn(*zombie, player + QcVec3{4.0, 0.0, 0.0}, 900001);
        // Far away: no goal is reported.
        m_server->mobs().spawn(*zombie, player + QcVec3{200.0, 0.0, 0.0}, 900002);

        m_client->connection.send(QcMsg::DebugSubscribe{QcMsg::DebugSubscribe::all});
        pump();
        // Within one look for players (QcMobSteering::sightCheckTicks) the zombie sees the player.
        ticks(QcMobSteering::sightCheckTicks + 2);
        QVERIFY(!m_client->debugMessages.isEmpty());
        const QList<QcMsg::MobGoal> goals = m_client->debugMessages.constLast().goals;
        const auto chase = std::ranges::find(goals, chaser, &QcMsg::MobGoal::entityId);
        QVERIFY(chase != goals.end());
        QCOMPARE(chase->kind, QcMsg::MobGoal::Kind::Chase);
        QVERIFY((chase->goal - m_client->motion.position).length() < 1.0);
        QVERIFY(std::ranges::find(goals, 900002u, &QcMsg::MobGoal::entityId) == goals.end());
    }

    void theLatestSubscriptionCounts()
    {
        m_origin = QcServer::ClientOrigin::Local;
        join();

        // A subscription is state: after 21 quick toggles the server follows the last one, even
        // though they came faster than any rate limit would let through.
        for (int i = 0; i <= 20; ++i) {
            m_client->connection.send(QcMsg::DebugSubscribe{quint8(i % 2 == 0 ? QcMsg::DebugSubscribe::stats : 0)});
        }
        pump();
        ticks(1);
        QCOMPARE(m_client->debugMessages.size(), 1);

        // Ending with "off" stops the messages.
        for (int i = 0; i < 20; ++i) {
            m_client->connection.send(QcMsg::DebugSubscribe{quint8(i % 2 == 0 ? QcMsg::DebugSubscribe::all : 0)});
        }
        pump();
        ticks(30);
        QCOMPARE(m_client->debugMessages.size(), 1);
    }

    void subscriptionFloodKeepsThePace()
    {
        m_origin = QcServer::ClientOrigin::Local;
        join();

        // A new subscription every tick does not bring a message every tick.
        for (int i = 0; i < 40; ++i) {
            m_client->connection.send(QcMsg::DebugSubscribe{quint8(i % 2 == 0 ? QcMsg::DebugSubscribe::stats : QcMsg::DebugSubscribe::all)});
            pump();
            ticks(1);
        }
        QCOMPARE(m_client->debugMessages.size(), 4);
        // Each tick's duration reaches the client once: a message reports the ticks since the previous one.
        const QList<QcMsg::ServerDebug> &messages = m_client->debugMessages;
        for (qsizetype i = 1; i < messages.size(); ++i) {
            QCOMPARE(quint64(messages[i].recentTickMs.size()), messages[i].tick - messages[i - 1].tick);
        }
    }

    void playersWithoutOperatorRightsGetNoDebugData()
    {
        // Only operators see the server's counts and whom creatures chase (widened in stage 24).
        m_origin = QcServer::ClientOrigin::Remote;
        join(notOperator());
        const std::optional<int> zombie = m_content->mobIndex(u"qtcraft:zombie"_s);
        QVERIFY(zombie.has_value());
        m_server->mobs().spawn(*zombie, m_client->motion.position + QcVec3{4.0, 0.0, 0.0}, 900001);

        m_client->connection.send(QcMsg::DebugSubscribe{QcMsg::DebugSubscribe::all});
        pump();
        ticks(40);
        QVERIFY(m_client->debugMessages.isEmpty());
        QCOMPARE(m_server->clientCount(), 1);
    }

    void remoteOperatorsGetDebugDataUntilDeopped()
    {
        m_origin = QcServer::ClientOrigin::Remote;
        join();
        m_client->connection.send(QcMsg::DebugSubscribe{QcMsg::DebugSubscribe::stats});
        pump();
        ticks(20);
        QVERIFY(!m_client->debugMessages.isEmpty());

        // The console takes the rights away: the subscription ends with them.
        m_server->runConsoleCommand(u"deop Raw"_s, {});
        m_client->debugMessages.clear();
        ticks(20);
        QVERIFY(m_client->debugMessages.isEmpty());
        m_client->connection.send(QcMsg::DebugSubscribe{QcMsg::DebugSubscribe::stats});
        pump();
        ticks(20);
        QVERIFY(m_client->debugMessages.isEmpty());
    }

    void countersAreCheap()
    {
        // A lap is one clock read and an addition: far below a microsecond even in debug and
        // sanitizer builds (a tick has 14 laps against its 50 ms).
        QcTickPhaseTimer timer;
        constexpr int laps = 200'000;
        QElapsedTimer elapsed;
        elapsed.start();
        timer.beginTick();
        for (int i = 0; i < laps; ++i) {
            timer.lap(QcTickPhase(i % QcTickPhases::inTick));
        }
        timer.endTick(0);
        const double perLapNs = double(elapsed.nsecsElapsed()) / laps;
        qInfo("%.1f ns per lap", perLapNs);
        QVERIFY2(perLapNs < 2000.0, qPrintable(u"%1 ns per lap"_s.arg(perLapNs)));
    }
};

QTEST_GUILESS_MAIN(TestTickPhases)
#include "test_tick_phases.moc"
