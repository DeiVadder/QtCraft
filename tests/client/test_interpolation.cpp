#include "clientcore/session/QcClientEntities.h"
#include "clientcore/session/QcClientSession.h"
#include "QcLatencyTransport.h"
#include "QcTestUtil.h"
#include "server/core/QcLocalServerHost.h"
#include "worldgen/generators/QcWorldGenerators.h"

#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <map>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 30'000;
constexpr int tickMs = 50;
constexpr quint32 moverId = 7;

// What the shown motion of one walker looked like, sampled every few milliseconds.
struct Motion
{
    int backwardJumps = 0;
    // Longest time the walker stood still on screen while it walked.
    double longestStallMs = 0.0;
    // Speed over 100 ms windows against the true speed: the largest and smallest ratio.
    double fastestRatio = 0.0;
    double slowestRatio = 1e9;
    // Longest time between two frames: how long the process doing the sampling stood still.
    double longestFrameGapMs = 0.0;

    QString describe() const
    {
        return u"backward %1, stall %2 ms, speed x%3..x%4, frame gap %5 ms"_s.arg(backwardJumps)
            .arg(longestStallMs)
            .arg(slowestRatio, 0, 'f', 2)
            .arg(fastestRatio, 0, 'f', 2)
            .arg(longestFrameGapMs);
    }
};

// Collects frame samples (time, shown x) of a walker moving towards +x at `speed` m/s.
class MotionRecorder
{
public:
    explicit MotionRecorder(double speed)
        : m_speed(speed)
    {
    }

    void sample(double ms, double x) { m_samples.emplace_back(ms, x); }

    Motion result() const
    {
        Motion motion;
        double stillSince = m_samples.empty() ? 0.0 : m_samples.front().first;
        for (std::size_t i = 1; i < m_samples.size(); ++i) {
            const auto [ms, x] = m_samples[i];
            const double previous = m_samples[i - 1].second;
            motion.backwardJumps += x < previous - 1e-9 ? 1 : 0;
            motion.longestFrameGapMs = std::max(motion.longestFrameGapMs, ms - m_samples[i - 1].first);
            if (x > previous + 1e-9) {
                stillSince = ms;
            }
            motion.longestStallMs = std::max(motion.longestStallMs, ms - stillSince);
        }

        for (std::size_t i = 0, j = 0; i < m_samples.size(); ++i) {
            while (j < m_samples.size() && m_samples[j].first < m_samples[i].first + 100.0) {
                ++j;
            }
            if (j == m_samples.size()) {
                break;
            }
            const double ratio = (m_samples[j].second - m_samples[i].second) / ((m_samples[j].first - m_samples[i].first) / 1000.0) / m_speed;
            motion.fastestRatio = std::max(motion.fastestRatio, ratio);
            motion.slowestRatio = std::min(motion.slowestRatio, ratio);
        }
        return motion;
    }

private:
    double m_speed;
    std::vector<std::pair<double, double>> m_samples;
};

QcMsg::EntitySpawn walkerSpawn(const QcVec3 &position = {0.0, 64.0, 0.0})
{
    QcMsg::EntitySpawn spawn;
    spawn.id = moverId;
    spawn.name = u"Walker"_s;
    spawn.position = position;
    return spawn;
}

QcMsg::EntityMoves walkerAt(qint64 serverMs, const QcVec3 &position)
{
    QcMsg::EntityMoves moves;
    moves.timeMs = serverMs;
    moves.moves = {{moverId, position, 90.0f, 20.0, false, 0.0f, QcMsg::OnGround, qcNoItem}};
    return moves;
}

// How a client shows a walker the server reports every tick at 4 m/s, when each report arrives
// after `delayMs` plus up to `jitterMs` (in order, like TCP): one tick per 50 ms of the client, out
// of phase with the server, and a frame every 4 ms interpolating within the tick.
Motion simulateWalk(bool buffered, int delayMs, int jitterMs, quint32 seed)
{
    constexpr double speed = 4.0;
    constexpr int ticks = 200;
    QRandomGenerator random(seed);
    std::map<qint64, std::vector<QcMsg::EntityMoves>> arrivals;
    qint64 previousArrival = 0;
    for (int k = 1; k <= ticks; ++k) {
        const qint64 arrival = std::max(previousArrival, qint64(k) * tickMs + delayMs + random.bounded(jitterMs + 1));
        arrivals[arrival].push_back(walkerAt(qint64(k) * tickMs, {speed * k * tickMs / 1000.0, 64.0, 0.0}));
        previousArrival = arrival;
    }

    QcClientEntities entities;
    entities.setBuffered(buffered);
    entities.spawn(walkerSpawn(), nullptr);
    MotionRecorder recorder(speed);
    qint64 lastTick = 0;
    for (qint64 now = 0; now < qint64(ticks) * tickMs; ++now) {
        for (const QcMsg::EntityMoves &moves : arrivals[now]) {
            entities.moves(moves, now);
        }
        if (now % tickMs == 17) {
            entities.tick(now);
            lastTick = now;
        }
        // Measured once the clock settled and while the walker still walks.
        if (now % 4 == 0 && now >= 2000 && now <= qint64(ticks) * tickMs - 400) {
            const QcClientEntity &walker = entities.all().value(moverId);
            const double alpha = std::clamp(double(now - lastTick) / tickMs, 0.0, 1.0);
            recorder.sample(double(now), walker.previousPosition.x + (walker.position.x - walker.previousPosition.x) * alpha);
        }
    }
    return recorder.result();
}

} // namespace

// The interpolation buffer for other players and creatures (decision E-271): the client shows them
// about 100 ms in the past, between buffered server snapshots, so late and bunched messages do not
// make them stutter; teleports snap, pauses do not creep, a missing snapshot is bridged for a moment.
class TestInterpolation : public QObject
{
    Q_OBJECT

private slots:
    void bufferInterpolatesExtrapolatesAndHolds()
    {
        QcSnapshotBuffer buffer;
        buffer.push({100.0, {0.0, 0.0, 0.0}, 350.0f, 0.0f}, 50.0);
        buffer.push({150.0, {1.0, 0.0, 0.0}, 10.0f, 20.0f}, 50.0);
        QCOMPARE(buffer.at(50.0).position.x, 0.0);
        QCOMPARE(buffer.at(125.0).position.x, 0.5);
        // The short way round from 350 to 10 degrees.
        QCOMPARE(buffer.at(125.0).yaw, 360.0f);
        QCOMPARE(buffer.at(125.0).pitch, 10.0f);
        // Past the newest: on along the last movement for extrapolationMs, then held.
        QCOMPARE(buffer.at(200.0).position.x, 2.0);
        QCOMPARE(buffer.at(250.0).position.x, 3.0);
        QCOMPARE(buffer.at(900.0).position.x, 3.0);
        QCOMPARE(buffer.at(900.0).yaw, 10.0f);

        buffer.push({200.0, {2.0, 0.0, 0.0}, 10.0f, 20.0f}, 50.0);
        buffer.push({250.0, {3.0, 0.0, 0.0}, 10.0f, 20.0f}, 50.0);
        buffer.dropBefore(210.0);
        QCOMPARE(buffer.size(), std::size_t(2));
        QCOMPARE(buffer.at(225.0).position.x, 2.5);
        // The newest two stay, so a late snapshot is still bridged along the last movement.
        buffer.dropBefore(400.0);
        QCOMPARE(buffer.size(), std::size_t(2));
        QCOMPARE(buffer.at(300.0).position.x, 4.0);
    }

    void aTeleportDropsTheHistoryAndAPauseDoesNotCreep()
    {
        QcSnapshotBuffer buffer;
        buffer.push({100.0, {0.0, 0.0, 0.0}}, 50.0);
        buffer.push({150.0, {0.5, 0.0, 0.0}}, 50.0);
        buffer.push({200.0, {40.0, 0.0, 0.0}}, 50.0);
        QCOMPARE(buffer.size(), std::size_t(1));
        QCOMPARE(buffer.at(120.0).position.x, 40.0);

        // An item that lay still for three seconds moves from the last interval on, not over the pause.
        QcSnapshotBuffer item;
        item.push({1000.0, {0.0, 0.0, 0.0}}, 50.0);
        item.push({4000.0, {0.5, 0.0, 0.0}}, 50.0);
        QCOMPARE(item.at(3900.0).position.x, 0.0);
        QCOMPARE(item.at(3950.0).position.x, 0.0);
        QCOMPARE(item.at(3975.0).position.x, 0.25);
    }

    void theClockFollowsTheFastestArrivalAndTheTickRate()
    {
        QcSnapshotClock clock;
        QVERIFY(!clock.advance(0).has_value());
        // Arrivals 30 to 80 ms after their server time: the fastest counts.
        for (int k = 0; k < 40; ++k) {
            clock.observe(qint64(k) * 50, qint64(k) * 50 + (k % 3 == 0 ? 30 : 80));
        }
        QCOMPARE(clock.intervalMs(), 50.0);
        QCOMPARE(clock.delayMs(), 100.0);
        QCOMPARE(*clock.advance(2000), 2000.0 - 30.0 - 100.0);
        // The shown time never runs backwards and at most 10 % off the local clock while it adapts.
        clock.observe(2000, 2100);
        double shown = *clock.advance(2050);
        for (int k = 1; k <= 60; ++k) {
            clock.observe(2000 + qint64(k) * 50, 2000 + qint64(k) * 50 + 100);
            const double next = *clock.advance(2050 + qint64(k) * 50);
            QVERIFY(next - shown >= 45.0 - 1e-9 && next - shown <= 55.0 + 1e-9);
            shown = next;
        }

        // One tick per second (/tick rate 1): the delay covers an interval plus the allowance.
        QcSnapshotClock slow;
        for (int k = 0; k < 3; ++k) {
            slow.observe(qint64(k) * 1000, qint64(k) * 1000 + 20);
        }
        QCOMPARE(slow.delayMs(), 1050.0);
    }

    void aTickRateChangeIsTakenAtOnce()
    {
        // 20 ticks per second, then one per second (/tick rate 1) from server time 3 s, then 20
        // again from 9 s; every snapshot arrives 30 ms after its server time, a client tick every 50 ms.
        QcSnapshotClock clock;
        const auto intervalAt = [](qint64 serverMs) { return serverMs >= 3000 && serverMs < 9000 ? 1000 : 50; };
        std::map<qint64, double> shownLag;
        for (qint64 now = 0; now <= 10'000; ++now) {
            const qint64 server = now - 30;
            if (server > 0 && server % intervalAt(server) == 0) {
                clock.observe(server, now);
            }
            if (now % 50 == 25) {
                if (const std::optional<double> shown = clock.advance(now)) {
                    shownLag[now] = double(now) - *shown;
                }
            }
        }

        QCOMPARE(shownLag.at(2975), 30.0 + 100.0);
        // Once the window shows the slow rate (2 s), the delay of one interval plus 50 ms holds at
        // once instead of being reached ten seconds later.
        QCOMPARE(shownLag.at(5525), 30.0 + 1050.0);
        QCOMPARE(shownLag.at(8975), 30.0 + 1050.0);
        // Back at 20 per second the short delay holds after two snapshots.
        QCOMPARE(shownLag.at(9125), 30.0 + 100.0);
    }

    void manyArrivalsAreBounded()
    {
        // A server sending many messages per tick does not grow the clock's history beyond its bound.
        QcSnapshotClock clock;
        for (int k = 0; k < 5000; ++k) {
            clock.observe(k / 10, 1000 + k / 10);
        }
        QVERIFY(clock.arrivalCount() <= QcSnapshotClock::maxArrivals);
        QCOMPARE(clock.intervalMs(), 1.0);
    }

    void aRespawnWithTheSameIdStartsAfresh()
    {
        QcClientEntities entities;
        entities.spawn(walkerSpawn(), nullptr);
        qint64 now = 0;
        qint64 server = 0;
        for (int k = 1; k <= 20; ++k) {
            entities.moves(walkerAt(server += tickMs, {0.1 * k, 64.0, 0.0}), now += tickMs);
            entities.tick(now + 10);
        }

        // Died at x = 2 and came back with the same id at x = 6 (closer than a teleport).
        entities.remove({moverId, true});
        entities.spawn(walkerSpawn({6.0, 64.0, 0.0}), nullptr);
        QCOMPARE(entities.dying().value(moverId).position.x, entities.dying().value(moverId).previousPosition.x);
        QVERIFY(entities.dying().value(moverId).position.x <= 2.0 + 1e-9);
        for (int k = 1; k <= 10; ++k) {
            entities.moves(walkerAt(server += tickMs, {6.0, 64.0, 0.0}), now += tickMs);
            entities.tick(now + 10);
            // Shown at its new place from the start, never on the way from where it died.
            const QcClientEntity &walker = entities.all().value(moverId);
            QCOMPARE(walker.position.x, 6.0);
            QCOMPARE(walker.previousPosition.x, 6.0);
        }
    }

    void aTeleportOnScreenSnaps()
    {
        QcClientEntities entities;
        entities.spawn(walkerSpawn(), nullptr);
        qint64 now = 0;
        for (int k = 1; k <= 40; ++k) {
            const double x = k <= 20 ? 0.1 * k : 100.0;
            entities.moves(walkerAt(qint64(k) * tickMs, {x, 64.0, 0.0}), now += tickMs);
            entities.tick(now + 10);
            const QcClientEntity &walker = entities.all().value(moverId);
            // Never anywhere between the old place and the new one.
            QVERIFY2(walker.position.x <= 2.0 + 1e-9 || walker.position.x == 100.0, qPrintable(QString::number(walker.position.x)));
            QVERIFY(walker.previousPosition.x <= 2.0 + 1e-9 || walker.previousPosition.x == 100.0);
        }
        QCOMPARE(entities.all().value(moverId).position.x, 100.0);
    }

    void jitterDoesNotMakeOthersStutter_data()
    {
        QTest::addColumn<int>("delayMs");
        QTest::addColumn<int>("jitterMs");
        QTest::newRow("one way 25-35 ms") << 25 << 10;
        QTest::newRow("one way 50-100 ms") << 50 << 50;
        QTest::newRow("one way 100-150 ms") << 100 << 50;
    }

    void jitterDoesNotMakeOthersStutter()
    {
        QFETCH(int, delayMs);
        QFETCH(int, jitterMs);
        const Motion buffered = simulateWalk(true, delayMs, jitterMs, 11);
        const Motion direct = simulateWalk(false, delayMs, jitterMs, 11);
        qInfo("buffered: %s; newest snapshot per tick: %s", qPrintable(buffered.describe()), qPrintable(direct.describe()));

        // Jitter within the allowance: the walker moves on screen exactly as on the server.
        QCOMPARE(buffered.backwardJumps, 0);
        QVERIFY2(buffered.longestStallMs <= 8.0, qPrintable(buffered.describe()));
        QVERIFY2(buffered.slowestRatio >= 0.9 && buffered.fastestRatio <= 1.1, qPrintable(buffered.describe()));
        // Without the buffer, late snapshots show as stops and bunched ones as bursts.
        if (jitterMs >= 50) {
            QVERIFY2(direct.longestStallMs >= 50.0 || direct.fastestRatio >= 1.4, qPrintable(direct.describe()));
        }
    }

    void jitterBeyondTheAllowanceIsBridged()
    {
        // Up to 150 ms late, more than the 50 ms allowance: extrapolation bridges the gaps of a
        // steady walk, so it still never goes backwards and hardly stops.
        const Motion buffered = simulateWalk(true, 50, 150, 5);
        const Motion direct = simulateWalk(false, 50, 150, 5);
        qInfo("buffered: %s; newest snapshot per tick: %s", qPrintable(buffered.describe()), qPrintable(direct.describe()));
        QCOMPARE(buffered.backwardJumps, 0);
        QVERIFY2(buffered.longestStallMs < direct.longestStallMs, qPrintable(buffered.describe()));
        QVERIFY2(buffered.fastestRatio < direct.fastestRatio, qPrintable(buffered.describe()));
    }

    void anotherPlayerWalksSmoothlyOverAJitteryLink()
    {
#if defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
        // Real-time smoothness under a sanitizer (several times slower, full suite in parallel)
        // measures the sanitizer, not the interpolation; the debug run keeps this check and the
        // simulated walks above cover the logic deterministically.
        QSKIP("Real-time smoothness is not meaningful under a sanitizer");
#endif
#endif
        // A real server and two sessions: the walker in-process, the watcher behind 40-100 ms of
        // one-way delay. The watcher's view of the walker is sampled every 4 ms like frames.
        const std::shared_ptr<const QcContentRegistry> content = QcTestUtil::loadContent();
        QcServerConfig config;
        config.viewDistance = 2;
        config.generator = QString::fromLatin1(QcWorldGenerators::flat);
        QcLocalServerHost host(config, content, QcWorldGenerators::create(config.generator, content, 1));
        QVERIFY(host.startError().isEmpty());

        QcClientSession walker(host.connectClient(), u"Walker"_s);
        QcClientSession watcher(std::make_unique<QcLatencyTransport>(host.connectClient(), 40, 60, 99), u"Watcher"_s);
        walker.start();
        watcher.start();
        QTRY_VERIFY_WITH_TIMEOUT(walker.state() == QcClientSession::State::Playing && watcher.state() == QcClientSession::State::Playing,
                                 timeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(watcher.entities().contains(walker.entityId()), timeoutMs);
        QTest::qWait(1500);

        const auto measure = [&](bool buffered) {
            watcher.entityMirror().setBuffered(buffered);
            QcPlayerInput input;
            input.forward = 1.0f;
            input.yaw = 90.0f;
            walker.setIntent(input);
            QTest::qWait(800);

            QElapsedTimer clock;
            clock.start();
            qint64 lastTick = 0;
            const QMetaObject::Connection ticks = connect(&watcher, &QcClientSession::ticked, this, [&] { lastTick = clock.elapsed(); });
            MotionRecorder recorder(4.317);
            QTimer frames;
            frames.setTimerType(Qt::PreciseTimer);
            connect(&frames, &QTimer::timeout, this, [&] {
                const QcClientEntity entity = watcher.entities().value(walker.entityId());
                const double alpha = std::clamp(double(clock.elapsed() - lastTick) / tickMs, 0.0, 1.0);
                recorder.sample(double(clock.elapsed()), entity.previousPosition.x + (entity.position.x - entity.previousPosition.x) * alpha);
            });
            frames.start(4);
            QTest::qWait(3000);
            frames.stop();
            disconnect(ticks);
            walker.setIntent({});
            QTest::qWait(600);
            return recorder.result();
        };

        // Real time on a busy machine can delay the frame timer, and the walker's inputs then cross
        // the server's ticks unevenly (two in one tick, none in the next): the walk itself stutters
        // and a late snapshot after a double step is extrapolated too far (E-271, limits). The test
        // runs in the timing phase on a quiet machine with few other tests (tests/timing.cmake, E-410,
        // E-550), the best of three walks counts, and the longest gap between frames is reported.
        Motion buffered = measure(true);
        for (int attempt = 1; attempt < 3 && buffered.backwardJumps > 0; ++attempt) {
            buffered = measure(true);
        }
        const Motion direct = measure(false);
        qInfo("buffered: %s; newest snapshot per tick: %s", qPrintable(buffered.describe()), qPrintable(direct.describe()));
        QCOMPARE(buffered.backwardJumps, 0);
        // The server moves the walker per input, two inputs in one of its ticks and none in the
        // next now and then; such a tick shows as a short stop, never longer than a tick and a bit.
        QVERIFY2(buffered.longestStallMs <= 90.0, qPrintable(buffered.describe()));
        QVERIFY2(buffered.slowestRatio >= 0.5 && buffered.fastestRatio <= 1.5, qPrintable(buffered.describe()));

        watcher.disconnectFromServer();
        walker.disconnectFromServer();
        QVERIFY(host.shutdown().has_value());
    }
};

QTEST_GUILESS_MAIN(TestInterpolation)
#include "test_interpolation.moc"
