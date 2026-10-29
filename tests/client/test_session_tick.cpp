// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/session/QcClientSession.h"
#include "clientcore/tick/QcSessionCore.h"
#include "clientcore/tick/QcTickPacer.h"
#include "clientcore/tick/QcTickTiming.h"
#include "common/physics/QcPhysics.h"
#include "QcRecordingTransport.h"
#include "QcTestUtil.h"
#include "server/core/QcServerConfig.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worlds/QcLocalServerHost.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <optional>
#include <ranges>
#include <utility>

using namespace Qt::StringLiterals;

// The client session's ticks (decision E-560): the pacer and the tick core on a fake clock, and in
// real time a session whose thread has long frames. A frame timer on the session's thread blocks it for `stallMs` every
// 300 ms; the session ticks by its timer against a local server. Measured: the intervals between
// the session's `ticked` signals (what the GUI sees), the punctuality of the tick core and how many
// ticks the core ran (each one input, one 50 ms step of the player on the server) and the session
// applied per tick due.
// QTCRAFT_TICK_MEASURE=full runs every stall length for longer and prints the table of E-560.
namespace {

constexpr int timeoutMs = 20000;
constexpr qint64 intervalNs = static_cast<qint64>(QcPhysics::tickDuration * 1e9);

bool fullMeasurement()
{
    return qEnvironmentVariable("QTCRAFT_TICK_MEASURE") == u"full"_s;
}

std::unique_ptr<QcLocalServerHost> makeHost(const std::shared_ptr<const QcContentRegistry> &content)
{
    QcServerConfig config;
    config.worldName = u"Tick"_s;
    config.seed = 7;
    config.viewDistance = 1;
    return std::make_unique<QcLocalServerHost>(config, content, std::make_unique<QcTestTerrainGenerator>(content));
}

// The intervals between the session's ticks as its thread saw them.
struct Intervals
{
    double p99Ms = 0.0;
    double maxMs = 0.0;
    // Intervals of more than 1.5 ticks: the player stood still on screen for a moment.
    int gaps = 0;
};

Intervals intervalsOf(QList<qint64> ns)
{
    Intervals result;
    if (ns.isEmpty()) {
        return result;
    }

    std::ranges::sort(ns);
    result.p99Ms = static_cast<double>(ns[(ns.size() * 99 + 99) / 100 - 1]) / 1e6;
    result.maxMs = static_cast<double>(ns.last()) / 1e6;
    result.gaps = static_cast<int>(std::ranges::count_if(ns, [](qint64 interval) { return interval > intervalNs * 3 / 2; }));
    return result;
}

struct Result
{
    QcTickTiming::Summary core;
    Intervals gui;
    // Ticks the core ran per tick due in the measured time (1.0: the player keeps the server's pace).
    double paceRatio = 0.0;
    // Ticks the session applied per tick due (lower when the measurement ends inside a long frame).
    double appliedRatio = 0.0;
};

} // namespace

class TestSessionTick : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    [[nodiscard]] static bool waitUntilPlaying(const QcClientSession &session)
    {
        return QTest::qWaitFor([&] { return session.state() == QcClientSession::State::Playing; }, timeoutMs);
    }

    // Plays for `measureMs` with a frame every 8 ms that blocks `stallMs` once every 300 ms.
    // Empty when the session does not start playing or the server does not shut down.
    std::optional<Result> measure(int stallMs, int measureMs)
    {
        auto host = makeHost(m_content);
        QcClientSession session(host->connectClient(), u"Ticker"_s);
        session.start();
        if (!waitUntilPlaying(session)) {
            return std::nullopt;
        }

        QElapsedTimer sinceStall;
        sinceStall.start();
        QTimer frames;
        frames.setInterval(8);
        QObject::connect(&frames, &QTimer::timeout, [&] {
            if (stallMs > 0 && sinceStall.elapsed() >= 300) {
                sinceStall.restart();
                QThread::msleep(static_cast<quint64>(stallMs));
            }
        });

        QElapsedTimer elapsed;
        QList<qint64> intervals;
        qint64 lastTickNs = -1;
        QObject::connect(&session, &QcClientSession::ticked, &frames, [&] {
            const qint64 now = elapsed.nsecsElapsed();
            if (lastTickNs >= 0) {
                intervals.append(now - lastTickNs);
            }
            lastTickNs = now;
        });

        session.tickTiming().reset();
        const quint64 ticksBefore = session.tickCount();
        elapsed.start();
        frames.start();
        // An event loop that sleeps until the next event (QTest::qWait polls every 10 ms).
        QEventLoop loop;
        QTimer::singleShot(measureMs, &loop, &QEventLoop::quit);
        loop.exec();
        frames.stop();

        Result result;
        result.core = session.tickTiming().summary();
        const double due = static_cast<double>(elapsed.nsecsElapsed()) / static_cast<double>(intervalNs);
        result.gui = intervalsOf(intervals);
        result.paceRatio = static_cast<double>(result.core.ticks) / due;
        result.appliedRatio = static_cast<double>(session.tickCount() - ticksBefore) / due;
        session.disconnectFromServer();
        if (!host->shutdown()) {
            return std::nullopt;
        }
        return result;
    }

private slots:
    void pacerRunsDueTicksAndCatchesUp()
    {
        QcTickPacer pacer(intervalNs);
        pacer.start(0);
        QCOMPARE(pacer.take(intervalNs - 1), 0);
        QCOMPARE(pacer.untilNextNs(intervalNs - 1), qint64(1));
        QCOMPARE(pacer.take(intervalNs), 1);
        QCOMPARE(pacer.untilNextNs(intervalNs), intervalNs);

        // A hold-up of three intervals: the three due ticks run back to back, none is lost.
        QCOMPARE(pacer.take(4 * intervalNs + 10), 3);
        QcTickTiming::Summary timing = pacer.timing().summary();
        QCOMPARE(timing.ticks, quint64(4));
        QCOMPARE(timing.missed, quint64(0));
        QCOMPARE(timing.lateMaxMs, double(2 * intervalNs + 10) / 1e6);
        QCOMPARE(pacer.untilNextNs(4 * intervalNs + 10), intervalNs - 10);

        // A hold-up of a second: the latest maxCatchUp ticks run, the older ones are dropped and the
        // grid stays where it was.
        const qint64 now = 4 * intervalNs + 20 * intervalNs;
        QCOMPARE(pacer.take(now), QcTickPacer::maxCatchUp);
        timing = pacer.timing().summary();
        QCOMPARE(timing.missed, quint64(20 - QcTickPacer::maxCatchUp));
        QCOMPARE(pacer.untilNextNs(now), intervalNs);
        QCOMPARE(pacer.take(now), 0);
    }

    void ticksAcrossLongFrames_data()
    {
        QTest::addColumn<int>("stallMs");
        QTest::newRow("no stalls") << 0;
        QTest::newRow("120 ms") << 120;
        if (fullMeasurement()) {
            QTest::newRow("30 ms") << 30;
            QTest::newRow("60 ms") << 60;
            QTest::newRow("250 ms") << 250;
            QTest::newRow("500 ms") << 500;
        }
    }

    void ticksAcrossLongFrames()
    {
        QFETCH(int, stallMs);
        const std::optional<Result> measured = measure(stallMs, fullMeasurement() ? 6000 : 1500);
        QVERIFY(measured);
        const Result &result = *measured;
        qInfo().noquote() << u"MEASURE stall_ms=%1 every_ms=300 pace=%2 applied=%11 gui_interval_p99_ms=%3 gui_interval_max_ms=%4 gui_gaps=%5 "
                             u"core_ticks=%6 core_missed=%7 core_late_avg_ms=%8 core_late_p99_ms=%9 core_late_max_ms=%10"_s
                                 .arg(stallMs)
                                 .arg(result.paceRatio, 0, 'f', 3)
                                 .arg(result.gui.p99Ms, 0, 'f', 1)
                                 .arg(result.gui.maxMs, 0, 'f', 1)
                                 .arg(result.gui.gaps)
                                 .arg(result.core.ticks)
                                 .arg(result.core.missed)
                                 .arg(result.core.lateAverageMs, 0, 'f', 2)
                                 .arg(result.core.lateP99Ms, 0, 'f', 2)
                                 .arg(result.core.lateMaxMs, 0, 'f', 2)
                                 .arg(result.appliedRatio, 0, 'f', 3);

        // The tick thread keeps the pace: no due tick is dropped (lenient bounds: other tests may run
        // beside this one). The core's own count, not the session's, which lags by the ticks of a
        // long frame still running when the measurement ends.
        QVERIFY(result.core.ticks > 0);
        if (qEnvironmentVariable("QTCRAFT_SESSION_THREAD") == u"0"_s) {
            // The comparison without the thread (E-560): measured, not checked.
            return;
        }
        if (stallMs <= 250) {
            QCOMPARE(result.core.missed, quint64(0));
        }
        QVERIFY2(result.paceRatio > 0.85, qPrintable(QString::number(result.paceRatio)));
    }

    void coreTicksOnItsClock()
    {
        // The tick core on a clock of the test's: nothing is due (and nothing counts as late) until
        // the world plays; commands apply before the ticks that follow them; a hold-up catches up
        // to maxCatchUp ticks and drops the rest; only mirrored commands count for the session.
        auto host = makeHost(m_content);
        std::vector<QcSessionEvent> events;
        QcSessionCore core(host->connectClient(), [&](QcSessionEvent &&event) { events.push_back(std::move(event)); });
        qint64 now = 0;
        core.setClock([&now] { return now; });
        core.apply({QcSessionCommand::Start{QcMsg::Hello{QcProtocol::version, u"Clock"_s, {}, {}}, true}});
        QVERIFY(QTest::qWaitFor(
            [&] {
                now += intervalNs;
                return core.runDueTicks() == 1;
            },
            timeoutMs));
        QCOMPARE(core.pacer().timing().summary().ticks, quint64(1));
        QCOMPARE(core.pacer().timing().summary().lateMaxMs, 0.0);

        const auto tickedEvents = [&events] {
            QList<QcSessionEvent::Ticked> ticked;
            for (const QcSessionEvent &event : events) {
                if (const auto *one = std::get_if<QcSessionEvent::Ticked>(&event.what)) {
                    ticked.append(*one);
                }
            }
            return ticked;
        };

        // Three ticks late: they run back to back with the intent given before.
        events.clear();
        QcPlayerInput forward;
        forward.forward = 1.0f;
        core.apply({QcSessionCommand::SetIntent{forward}});
        now += 3 * intervalNs;
        QCOMPARE(core.runDueTicks(), 3);
        QList<QcSessionEvent::Ticked> ticked = tickedEvents();
        QCOMPARE(ticked.size(), 3);
        for (const QcSessionEvent::Ticked &one : std::as_const(ticked)) {
            QVERIFY(one.predicted);
            QCOMPARE(one.after.prediction.intent().forward, 1.0f);
            QCOMPARE(one.after.mirroredApplied, quint64(0));
        }
        QCOMPARE(core.runDueTicks(), 0);

        // A wish to fly counts for the session, an intent does not.
        core.apply({QcSessionCommand::SetFlight{true}});
        core.apply({QcSessionCommand::SetIntent{QcPlayerInput{}}});
        events.clear();
        now += intervalNs;
        QCOMPARE(core.runDueTicks(), 1);
        ticked = tickedEvents();
        QCOMPARE(ticked.size(), 1);
        QCOMPARE(ticked.first().after.mirroredApplied, quint64(1));

        // A second's hold-up: the latest maxCatchUp ticks run, the older ones are dropped.
        core.pacer().timing().reset();
        events.clear();
        now += 20 * intervalNs;
        QCOMPARE(core.runDueTicks(), QcTickPacer::maxCatchUp);
        QCOMPARE(tickedEvents().size(), QcTickPacer::maxCatchUp);
        QCOMPARE(core.pacer().timing().summary().missed, quint64(20 - QcTickPacer::maxCatchUp));
        QCOMPARE(core.pacer().untilNextNs(now), intervalNs);

        core.shutdown();
        QVERIFY(host->shutdown().has_value());
    }

    void coreRestsWhileTheServerPauses()
    {
        // The host's own player asks for the pause (decision E-877): once the server says it paused,
        // the core's ticks predict nothing and send no input, so the player neither drifts nor
        // piles up inputs for the resumed server; after the resume it predicts again.
        auto host = makeHost(m_content);
        auto recording = std::make_unique<QcRecordingTransport>(host->connectClient());
        const QcRecordingTransport *wire = recording.get();
        std::vector<QcSessionEvent> events;
        QcSessionCore core(std::move(recording), [&](QcSessionEvent &&event) { events.push_back(std::move(event)); });
        qint64 now = 0;
        core.setClock([&now] { return now; });
        core.apply({QcSessionCommand::Start{QcMsg::Hello{QcProtocol::version, u"Rest"_s, {}, {}}, true}});
        QVERIFY(QTest::qWaitFor(
            [&] {
                now += intervalNs;
                return core.runDueTicks() == 1;
            },
            timeoutMs));

        const auto heardPause = [&events](bool paused) {
            return std::ranges::any_of(events, [paused](const QcSessionEvent &event) {
                const auto *received = std::get_if<QcSessionEvent::Received>(&event.what);
                const auto *pause = received ? std::get_if<QcMsg::ServerPause>(&received->message) : nullptr;
                return pause && pause->paused == paused;
            });
        };
        const auto lastTickPredicted = [&events] {
            for (const QcSessionEvent &event : std::ranges::reverse_view(events)) {
                if (const auto *ticked = std::get_if<QcSessionEvent::Ticked>(&event.what)) {
                    return ticked->predicted;
                }
            }
            return false;
        };

        core.apply({QcSessionCommand::Send{QcMsg::PauseRequest{true}}});
        QVERIFY(QTest::qWaitFor([&] { return heardPause(true); }, timeoutMs));
        const qsizetype inputs = wire->sentOf<QcMsg::PlayerInput>().size();
        for (int i = 0; i < 10; ++i) {
            now += intervalNs;
            QCOMPARE(core.runDueTicks(), 1);
            QVERIFY(!lastTickPredicted());
        }
        QCOMPARE(wire->sentOf<QcMsg::PlayerInput>().size(), inputs);

        core.apply({QcSessionCommand::Send{QcMsg::PauseRequest{false}}});
        QVERIFY(QTest::qWaitFor([&] { return heardPause(false); }, timeoutMs));
        now += intervalNs;
        QCOMPARE(core.runDueTicks(), 1);
        QVERIFY(lastTickPredicted());
        QCOMPARE(wire->sentOf<QcMsg::PlayerInput>().size(), inputs + 1);

        core.shutdown();
        QVERIFY(host->shutdown().has_value());
    }

    void longFramesDoNotHoldUpTheTickThread()
    {
        // While the session's thread is blocked, the core ticks on and walks the player with the
        // intent it has; the session applies those ticks in one go as soon as its thread runs again.
        auto host = makeHost(m_content);
        QcClientSession session(host->connectClient(), u"Ticker"_s);
        session.start();
        QVERIFY(waitUntilPlaying(session));
        QVERIFY(QTest::qWaitFor([&] { return session.playerMotion().onGround; }, timeoutMs));

        QcPlayerInput forward = session.intent();
        forward.forward = 1.0f;
        session.setIntent(forward);
        const QcVec3 standing = session.playerMotion().position;
        QVERIFY(QTest::qWaitFor([&] { return (session.playerMotion().position - standing).length() > 0.1; }, timeoutMs));

        int burst = 0;
        const QMetaObject::Connection counting = connect(&session, &QcClientSession::ticked, this, [&burst] { ++burst; });
        const QcVec3 before = session.playerMotion().position;
        const quint64 coreBefore = session.tickTiming().summary().ticks;
        QThread::msleep(300);
        const quint64 coreDuring = session.tickTiming().summary().ticks - coreBefore;
        QVERIFY2(coreDuring >= 3, qPrintable(QString::number(coreDuring)));

        // One pass of the event loop applies every tick the core ran meanwhile.
        QCoreApplication::processEvents();
        disconnect(counting);
        QVERIFY2(std::cmp_greater_equal(burst, coreDuring), qPrintable(u"%1 of %2"_s.arg(burst).arg(coreDuring)));
        const double walked = (session.playerMotion().position - before).length();
        QVERIFY2(walked > double(coreDuring) * QcPhysics::tickDuration * 1.0, qPrintable(QString::number(walked)));

        session.disconnectFromServer();
        QVERIFY(host->shutdown().has_value());
    }

    void commandsReachTheServerInOrder()
    {
        // Hotbar choices and view changes made in one go on the session's thread go out in that
        // order: the server gets every choice, the last one last, and the inputs after them carry the
        // last view.
        auto host = makeHost(m_content);
        auto recording = std::make_unique<QcRecordingTransport>(host->connectClient());
        const QcRecordingTransport *wire = recording.get();
        QcClientSession session(std::move(recording), u"Ticker"_s);
        session.start();
        QVERIFY(waitUntilPlaying(session));

        QList<qint32> chosen;
        for (int round = 0; round < 3; ++round) {
            for (int slot = 0; slot < 9; ++slot) {
                session.selectHotbarSlot(slot);
                chosen.append(slot);
                QcPlayerInput intent = session.intent();
                intent.yaw = static_cast<float>(slot * 10);
                session.setIntent(intent);
            }
        }
        session.selectHotbarSlot(4);
        chosen.append(4);

        const auto slotsSent = [wire] {
            QList<qint32> sentSlots;
            const QList<QcMessage> sent = wire->sent();
            for (const QcMessage &message : sent) {
                if (const auto *select = std::get_if<QcMsg::SelectHotbarSlot>(&message)) {
                    sentSlots.append(select->slot);
                }
            }
            return sentSlots;
        };
        QTRY_COMPARE_WITH_TIMEOUT(slotsSent(), chosen, timeoutMs);
        const qsizetype inputsBefore = wire->sentOf<QcMsg::PlayerInput>().size();
        QTRY_VERIFY_WITH_TIMEOUT(wire->sentOf<QcMsg::PlayerInput>().size() >= inputsBefore + 2, timeoutMs);
        const QList<QcMsg::PlayerInput> inputs = wire->sentOf<QcMsg::PlayerInput>();
        QCOMPARE(inputs.constLast().input.yaw, 80.0f);
        // The server's answer names the last slot.
        QTRY_COMPARE_WITH_TIMEOUT(session.inventory().selectedHotbarSlot(), 4, timeoutMs);
        session.disconnectFromServer();
        QVERIFY(host->shutdown().has_value());
    }

    void sessionsEndWithoutRaces()
    {
        // Sessions are deleted while their thread ticks: right after leaving, without leaving,
        // before playing and without ever starting. The server sees every player go.
        auto host = makeHost(m_content);
        for (int round = 0; round < 8; ++round) {
            auto session = std::make_unique<QcClientSession>(host->connectClient(), u"Ticker%1"_s.arg(round));
            if (round % 4 != 3) {
                session->start();
            }
            if (round % 4 == 0) {
                QVERIFY(waitUntilPlaying(*session));
                session->disconnectFromServer();
            } else if (round % 4 == 1) {
                QVERIFY(waitUntilPlaying(*session));
            }
            session.reset();
        }
        QTRY_VERIFY_WITH_TIMEOUT(host->runConsoleCommand(u"list"_s).join(u' ').contains(u"0 players online"_s), timeoutMs);
        QVERIFY(host->shutdown().has_value());
    }

    void withoutThreadTheTimerTicksInTheSessionsThread()
    {
        // QTCRAFT_SESSION_THREAD=0: the same session, ticking in its own thread.
        qputenv("QTCRAFT_SESSION_THREAD", "0");
        auto host = makeHost(m_content);
        QcClientSession session(host->connectClient(), u"Ticker"_s);
        session.start();
        qunsetenv("QTCRAFT_SESSION_THREAD");
        QVERIFY(waitUntilPlaying(session));

        const quint64 before = session.tickCount();
        QThread::msleep(120);
        // Nothing ticks while this thread sleeps; afterwards the missed ticks run back to back.
        QCOMPARE(session.tickCount(), before);
        QTRY_VERIFY_WITH_TIMEOUT(session.tickCount() >= before + 2, timeoutMs);
        QCOMPARE(session.tickTiming().summary().missed, quint64(0));
        session.disconnectFromServer();
        QVERIFY(host->shutdown().has_value());
    }
};

QTEST_GUILESS_MAIN(TestSessionTick)
#include "test_session_tick.moc"
