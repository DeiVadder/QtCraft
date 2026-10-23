#include "QcTestUtil.h"
#include "server/core/QcServer.h"
#include "server/tick/QcTickScheduler.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"

#include <QEventLoop>
#include <QRandomGenerator>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <optional>

using namespace Qt::StringLiterals;

namespace {

constexpr qint64 ms = 1'000'000;

// A simulated clock and event loop: the timer fires when the scheduler asked for (plus a delay),
// and ticks cost simulated time.
struct Simulation
{
    qint64 now = 1'000 * ms;
    qint64 started = 0;
    quint64 ticks = 0;
    QcTickScheduler scheduler{[this] { return now; }};

    void start()
    {
        started = now;
        scheduler.start();
        now = scheduler.nextDueNs();
    }

    // One timer firing: runs the due ticks, each costing tickCost(tick number), then waits for the
    // next firing (the remaining time plus `timerDelay`).
    void fire(const std::function<qint64(quint64)> &tickCost, qint64 timerDelay)
    {
        const qint64 remaining = scheduler.runDue([&] {
            ++ticks;
            now += tickCost(ticks);
        });
        now += remaining + timerDelay;
    }

    // Ticks the grid has had due by now since start().
    quint64 gridTicks() const { return static_cast<quint64>((now - started) / QcTickScheduler::intervalNs); }
};

} // namespace

class TestTickScheduler : public QObject
{
    Q_OBJECT

private slots:
    void firstTickIsDueOneIntervalAfterStart()
    {
        Simulation simulation;
        simulation.start();
        QCOMPARE(simulation.now - simulation.started, QcTickScheduler::intervalNs);

        // Fired 1 ms early (rounding of the timer): nothing runs, the rest of the wait is returned.
        simulation.now -= ms;
        QCOMPARE(simulation.scheduler.runDue([] { QFAIL("no tick is due yet"); }), ms);
        QCOMPARE(simulation.scheduler.stats().summary().ticks, quint64(0));
    }

    void punctualTimerKeepsRealTime()
    {
        Simulation simulation;
        simulation.start();
        for (int i = 0; i < 1000; ++i) {
            simulation.fire([](quint64) { return 2 * ms; }, 0);
        }

        // 1000 firings at the due times: one tick each, never late.
        QCOMPARE(simulation.ticks, quint64(1000));
        const QcTickStats::Summary stats = simulation.scheduler.stats().summary();
        QCOMPARE(stats.ticks, quint64(1000));
        QCOMPARE(stats.lateMaxMs, 0.0);
        QCOMPARE(stats.skipped, quint64(0));
        QCOMPARE(stats.averageMs, 2.0);
    }

    void longTicksWithinTheLimitLoseNoGameTime()
    {
        // Every tenth tick costs 180 ms (3.6 intervals): at most four ticks fall due at once.
        Simulation simulation;
        simulation.start();
        while (simulation.now - simulation.started < 60'000 * ms) {
            simulation.fire([](quint64 tick) { return tick % 10 == 0 ? 180 * ms : 3 * ms; }, 0);
        }

        // 60 s of real time are 1200 ticks of game time; the slow ones only delay their successors.
        const QcTickStats::Summary stats = simulation.scheduler.stats().summary();
        QCOMPARE(stats.skipped, quint64(0));
        QVERIFY2(qAbs(qint64(simulation.ticks) - qint64(simulation.gridTicks())) <= 1,
                 qPrintable(u"%1 ticks for %2 due"_s.arg(simulation.ticks).arg(simulation.gridTicks())));
        QVERIFY(stats.lateMaxMs >= 130.0);
    }

    void theRateChangesTheInterval()
    {
        Simulation simulation;
        simulation.start();
        simulation.scheduler.setRate(5.0);
        QCOMPARE(simulation.scheduler.currentIntervalNs(), 200 * ms);
        QCOMPARE(simulation.scheduler.nextDueNs() - simulation.now, 200 * ms);

        // Five ticks per second: a second of time runs five ticks, with no catch-up warning.
        const quint64 before = simulation.ticks;
        simulation.now = simulation.scheduler.nextDueNs();
        for (int i = 0; i < 5; ++i) {
            simulation.fire([](quint64) { return ms; }, 0);
        }
        QCOMPARE(simulation.ticks - before, quint64(5));
        QCOMPARE(simulation.scheduler.stats().skipped(), quint64(0));
        QCOMPARE(qRound(simulation.scheduler.measuredRate()), 5);

        // Limits and the way back.
        simulation.scheduler.setRate(1000.0);
        QCOMPARE(simulation.scheduler.rate(), QcTickScheduler::maxRate);
        simulation.scheduler.setRate(0.0);
        QCOMPARE(simulation.scheduler.rate(), QcTickScheduler::minRate);
        simulation.scheduler.setRate(QcTickScheduler::defaultRate);
        QCOMPARE(simulation.scheduler.currentIntervalNs(), QcTickScheduler::intervalNs);
    }

    void aSprintRunsBackToBackAndLeavesNoBacklog()
    {
        Simulation simulation;
        simulation.start();
        simulation.scheduler.startSprint(1000);
        QVERIFY(simulation.scheduler.sprinting());

        // Each tick costs 1 ms: a slice of 20 ms runs 20 ticks and asks to be called at once again.
        const qint64 remaining = simulation.scheduler.runDue([&] {
            ++simulation.ticks;
            simulation.now += ms;
        });
        QCOMPARE(remaining, 0);
        QCOMPARE(simulation.ticks, quint64(20));
        while (simulation.scheduler.sprinting()) {
            simulation.fire([](quint64) { return ms; }, 0);
        }
        QCOMPARE(simulation.ticks, quint64(1000));

        // One second passed for 1000 ticks; afterwards the normal grid starts anew without catching up.
        const std::optional<QcTickScheduler::SprintEnd> ended = simulation.scheduler.takeSprintEnded();
        QVERIFY(ended);
        QCOMPARE(ended->ticks, quint64(1000));
        QVERIFY(!simulation.scheduler.takeSprintEnded());
        // The last slice asked for one interval of rest: the next tick is due exactly then.
        QCOMPARE(simulation.scheduler.nextDueNs(), simulation.now);
        simulation.fire([](quint64) { return ms; }, 0);
        QCOMPARE(simulation.ticks, quint64(1001));
        QCOMPARE(simulation.scheduler.stats().skipped(), quint64(0));
        QCOMPARE(simulation.scheduler.stats().summary().lateMaxMs, 0.0);
    }

    void aSprintTickStandsForItsRealTime()
    {
        // What follows real time (budgets, limits, autosaves) gets the real time of a sprint tick,
        // never a whole interval per tick (decision E-249).
        Simulation simulation;
        simulation.start();
        QCOMPARE(simulation.scheduler.tickRealNs(), QcTickScheduler::intervalNs);
        QList<qint64> realTimes;
        simulation.scheduler.startSprint(10);
        simulation.now += 3 * ms;
        simulation.scheduler.runDue([&] {
            realTimes.append(simulation.scheduler.tickRealNs());
            simulation.now += ms;
        });
        QCOMPARE(realTimes, (QList<qint64>{3 * ms, ms, ms, ms, ms, ms, ms, ms, ms, ms}));
        QCOMPARE(simulation.scheduler.tickRealNs(), QcTickScheduler::intervalNs);

        // A pause of the event loop counts up to one interval.
        realTimes.clear();
        simulation.scheduler.startSprint(2);
        simulation.now += 5'000 * ms;
        simulation.scheduler.runDue([&] { realTimes.append(simulation.scheduler.tickRealNs()); });
        QCOMPARE(realTimes, (QList<qint64>{QcTickScheduler::intervalNs, 0}));

        // Normal ticks stand for one interval of their rate.
        simulation.scheduler.setRate(1.0);
        simulation.now = simulation.scheduler.nextDueNs();
        realTimes.clear();
        simulation.scheduler.runDue([&] { realTimes.append(simulation.scheduler.tickRealNs()); });
        QCOMPARE(realTimes, QList<qint64>{1'000 * ms});
    }

    void aSprintCanBeStopped()
    {
        Simulation simulation;
        simulation.start();
        simulation.scheduler.startSprint(100'000);
        simulation.fire([](quint64) { return ms; }, 0);
        simulation.scheduler.stopSprint();
        QVERIFY(!simulation.scheduler.sprinting());
        const std::optional<QcTickScheduler::SprintEnd> ended = simulation.scheduler.takeSprintEnded();
        QVERIFY(ended);
        QCOMPARE(ended->ticks, quint64(20));
        QCOMPARE(simulation.scheduler.nextDueNs() - simulation.now, QcTickScheduler::intervalNs);
    }

    void backlogBeyondTheLimitIsDropped()
    {
        Simulation simulation;
        simulation.start();

        // The first tick takes one second: 20 ticks are then due, 5 run and 15 are dropped.
        QTest::ignoreMessage(QtWarningMsg, "Server overloaded, skipped 15 ticks");
        simulation.fire([](quint64 tick) { return tick == 1 ? 1'000 * ms : 0; }, 0);
        simulation.fire([](quint64) { return 0; }, 0);

        QCOMPARE(simulation.ticks, quint64(1 + QcTickScheduler::maxCatchUpTicks));
        QCOMPARE(simulation.scheduler.stats().summary().skipped, quint64(15));
        // The grid continues: the next tick is due one interval after the last one ran.
        QCOMPARE(simulation.scheduler.nextDueNs() - simulation.started, 22 * QcTickScheduler::intervalNs);
    }

    void lateTimersAndUnevenTicksDoNotDrift()
    {
        QRandomGenerator random(7);
        Simulation simulation;
        simulation.start();
        for (int i = 0; i < 100'000; ++i) {
            const qint64 delay = static_cast<qint64>(random.bounded(40)) * ms + static_cast<qint64>(random.bounded(1'000'000));
            simulation.fire([&random](quint64) { return static_cast<qint64>(random.bounded(20)) * ms; }, delay);
        }

        // About 2.5 simulated hours: ticks and real time still agree to one tick.
        const QcTickStats::Summary stats = simulation.scheduler.stats().summary();
        QCOMPARE(stats.skipped, quint64(0));
        QVERIFY(simulation.gridTicks() > 100'000);
        QVERIFY2(qAbs(qint64(simulation.ticks) - qint64(simulation.gridTicks())) <= 1,
                 qPrintable(u"%1 ticks for %2 due"_s.arg(simulation.ticks).arg(simulation.gridTicks())));
    }

    void lateAverageAndPercentile()
    {
        QcTickStats stats;
        // Lateness 1 .. 100 ms in shuffled order.
        for (int i = 0; i < 100; ++i) {
            stats.record(static_cast<qint64>((i * 37) % 100 + 1) * ms, 0);
        }
        stats.addSkipped(3);

        const QcTickStats::Summary summary = stats.summary();
        QCOMPARE(summary.ticks, quint64(100));
        QCOMPARE(summary.lateAverageMs, 50.5);
        QCOMPARE(summary.lateP99Ms, 99.0);
        QCOMPARE(summary.lateMaxMs, 100.0);
        QCOMPARE(summary.skipped, quint64(3));

        stats.reset();
        QCOMPARE(stats.summary().ticks, quint64(0));
        QCOMPARE(stats.summary().skipped, quint64(0));
    }

    void serverTicksInRealTime()
    {
        const std::shared_ptr<const QcContentRegistry> content = QcTestUtil::loadContent();
        QcServer server({}, content, std::make_unique<QcTestTerrainGenerator>(content));
        QVERIFY(server.openWorld().has_value());

        server.start();
        QTest::qWait(1000);
        QVERIFY(server.stop().has_value());

        // 20 ticks per second; a loaded machine may delay the last timer, but catching up keeps the
        // count close.
        const quint64 ticks = server.tickCount();
        QVERIFY2(ticks >= 16 && ticks <= 21, qPrintable(QString::number(ticks)));
        QCOMPARE(server.tickTimes().ticks, ticks);
    }

    void serverTicksStartOnTime()
    {
        // The event loop's timers fire 1-3 ms late on macOS; the timer armed early plus the wait for
        // the rest keeps the ticks within a few tenths of a millisecond. A timer on the due time
        // itself averages 1.4-2.2 ms here. An oversubscribed machine (sanitizer suites in parallel)
        // can delay a whole run, so the best of three runs has to meet the loose bound.
        double best = 0.0;
        for (int attempt = 0; attempt < 3; ++attempt) {
            const std::optional<double> late = lateAverageOfARun();
            QVERIFY(late.has_value());

            best = attempt == 0 ? *late : std::min(best, *late);
            if (best < 1.0) {
                break;
            }
        }

        QVERIFY2(best < 1.0, qPrintable(QString::number(best)));
    }

private:
    // Average lateness of the ticks of a 2 s server run in a real event loop (QTest::qWait sleeps
    // 10 ms between rounds of events); empty when the run failed.
    static std::optional<double> lateAverageOfARun()
    {
        const std::shared_ptr<const QcContentRegistry> content = QcTestUtil::loadContent();
        QcServer server({}, content, std::make_unique<QcTestTerrainGenerator>(content));
        if (!server.openWorld().has_value()) {
            return std::nullopt;
        }

        QEventLoop loop;
        QTimer::singleShot(2000, &loop, &QEventLoop::quit);
        server.start();
        loop.exec();
        if (!server.stop().has_value()) {
            return std::nullopt;
        }

        const QcTickStats::Summary stats = server.tickTimes();
        if (stats.ticks < 30) {
            return std::nullopt;
        }
        return stats.lateAverageMs;
    }
};

QTEST_GUILESS_MAIN(TestTickScheduler)
#include "test_tick_scheduler.moc"
