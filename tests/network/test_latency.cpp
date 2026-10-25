#include "clientcore/session/QcClientSession.h"
#include "common/gameplay/QcMining.h"
#include "common/physics/QcPhysics.h"
#include "QcLatencyTransport.h"
#include "QcTestUtil.h"
#include "server/core/QcServerConfig.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worlds/QcLocalServerHost.h"

#include <QElapsedTimer>
#include <QTest>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 30'000;

QcBlockPos blockOf(const QcVec3 &position)
{
    return QcPhysics::blockContaining(position);
}

} // namespace

// Client prediction with server correction under network latency (decision E-257): a transport
// delays every frame by 25-100 ms plus jitter in each direction (round trips of 50 to 300 ms).
// Walking, turning and jumping must need no correction at all; mining and placing take the
// round trip but are not refused or undone.
class TestLatency : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    std::unique_ptr<QcLocalServerHost> m_host;
    std::unique_ptr<QcClientSession> m_session;
    QcLatencyTransport *m_link = nullptr;

    void start(int delayMs, int jitterMs)
    {
        QcServerConfig config;
        config.viewDistance = 2;
        config.seed = 1;
        config.generator = QString::fromLatin1(QcWorldGenerators::flat);
        m_host = std::make_unique<QcLocalServerHost>(config, m_content, QcWorldGenerators::create(config.generator, m_content, 1));
        QVERIFY(m_host->startError().isEmpty());
        // A negative delay connects without the delaying link, like single player.
        std::unique_ptr<QcTransport> transport = m_host->connectClient();
        if (delayMs >= 0) {
            auto link = std::make_unique<QcLatencyTransport>(std::move(transport), delayMs, jitterMs, static_cast<quint32>(delayMs * 31 + jitterMs));
            m_link = link.get();
            transport = std::move(link);
        }
        m_session = std::make_unique<QcClientSession>(std::move(transport), u"Player"_s);
        m_session->start();
        QTRY_VERIFY_WITH_TIMEOUT(m_session->state() == QcClientSession::State::Playing && m_session->playerMotion().onGround, timeoutMs);
        // Settle: the first states move the player from the origin to the spawn.
        QTest::qWait(2 * (std::max(delayMs, 0) + jitterMs) + 200);
        m_session->resetCorrections();
    }

    // Real milliseconds per client tick during the last drive().
    double m_lastMsPerTick = 0.0;

    // Runs `ticks` client ticks with an intent made per tick; returns the most inputs waiting for
    // an answer of the server.
    template<typename Intent>
    qsizetype drive(int ticks, Intent intent)
    {
        qsizetype mostPending = 0;
        int tick = 0;
        QElapsedTimer elapsed;
        elapsed.start();
        QMetaObject::Connection connection = connect(m_session.get(), &QcClientSession::ticked, this, [&] {
            m_session->setIntent(intent(tick++));
            mostPending = std::max(mostPending, m_session->prediction().pendingInputCount());
        });
        // A run that does not finish shows as too few pending inputs in the checks.
        [[maybe_unused]] const bool finished = QTest::qWaitFor([&] { return tick >= ticks; }, timeoutMs);
        disconnect(connection);
        m_session->setIntent({});
        m_lastMsPerTick = tick > 0 ? static_cast<double>(elapsed.elapsed()) / tick : 0.0;
        return mostPending;
    }

    void stop()
    {
        m_session->disconnectFromServer();
        m_session.reset();
        QVERIFY(m_host->shutdown().has_value());
        m_host.reset();
    }

    // How long the server takes to confirm the block the client mines (straight down; pitch -90).
    double mineDown()
    {
        const QcBlockPos below = blockOf(m_session->playerMotion().position).offset(0, -1, 0);
        const QcBlockId block = *m_session->world()->blockAt(below);
        QcPlayerInput look;
        look.pitch = -90.0f;
        m_session->setIntent(look);
        QElapsedTimer timer;
        timer.start();
        m_session->setMining(true);
        if (!QTest::qWaitFor([&] { return m_session->world()->blockAt(below) == std::optional<QcBlockId>(qcAirBlockId); }, timeoutMs)) {
            return -1.0;
        }
        m_session->setMining(false);
        qInfo("mined %s in %lld ms", qPrintable(m_content->block(block).name), static_cast<qlonglong>(timer.elapsed()));
        return static_cast<double>(timer.elapsed()) / 1000.0;
    }

private slots:
    void cleanup()
    {
        if (m_host) {
            stop();
        }
    }

    void predictionNeedsNoCorrection_data()
    {
        QTest::addColumn<int>("delayMs");
        QTest::addColumn<int>("jitterMs");
        QTest::newRow("rtt 50-70 ms") << 25 << 10;
        QTest::newRow("rtt 100-150 ms") << 50 << 25;
        QTest::newRow("rtt 200-300 ms") << 100 << 50;
    }

    void predictionNeedsNoCorrection()
    {
        QFETCH(int, delayMs);
        QFETCH(int, jitterMs);
        start(delayMs, jitterMs);

        // Walking, sprinting, turning and jumping on flat ground for three seconds.
        const qsizetype mostPending = drive(60, [](int tick) {
            QcPlayerInput input;
            input.forward = 1.0f;
            input.strafe = tick % 40 < 20 ? 0.0f : 0.5f;
            input.sprint = tick >= 30;
            input.jump = tick % 15 == 0;
            input.yaw = static_cast<float>(tick * 3 % 360);
            return input;
        });
        QTest::qWait(2 * (std::max(delayMs, 0) + jitterMs) + 200);

        // Every server state matched the prediction it answered: no correction, so no jump on screen.
        qInfo("largest correction %.6f m, most pending inputs %lld, largest one-way delay %lld ms", m_session->prediction().largestCorrection(),
              static_cast<qlonglong>(mostPending), static_cast<qlonglong>(m_link->largestDelayMs()));
        QVERIFY2(m_session->prediction().largestCorrection() < 1e-6, qPrintable(QString::number(m_session->prediction().largestCorrection())));
        // The client sends an input every tick, also standing still: a round trip of them waits.
        QVERIFY(m_session->prediction().pendingInputCount() <= (2 * (delayMs + jitterMs)) / 50 + 3);
        // The inputs in flight are what the round trip holds, far from the limits of client and server.
        // The client sends one per its tick: a slower tick (sanitizers, a busy machine) holds fewer.
        const double msPerTick = std::max(50.0, m_lastMsPerTick);
        QVERIFY2(mostPending >= qsizetype(2 * delayMs / msPerTick) - 1,
                 qPrintable(u"%1 pending at %2 ms per tick"_s.arg(mostPending).arg(msPerTick)));
        QVERIFY(mostPending <= (2 * (delayMs + jitterMs)) / 50 + 3);
        QVERIFY(m_link->largestDelayMs() >= delayMs);
    }

    void aDisagreeingServerIsFollowed()
    {
        start(50, 25);
        // A wall the client does not know of yet: the server's state wins, once, by the wall.
        const QcVec3 at = m_session->playerMotion().position;
        const QcBlockPos feet = blockOf(at);
        for (int dy = 0; dy <= 2; ++dy) {
            m_host->runConsoleCommand(u"setblock %1 %2 %3 stone"_s.arg(feet.x).arg(feet.y + dy).arg(feet.z + 2));
        }
        drive(20, [](int) {
            QcPlayerInput input;
            input.forward = 1.0f;
            input.yaw = 0.0f;
            return input;
        });
        QTest::qWait(500);
        QVERIFY(m_session->playerMotion().position.z < double(feet.z + 2));
        QVERIFY(m_session->prediction().largestCorrection() < 2.0);
    }

    void miningAndPlacingWithLatency_data()
    {
        QTest::addColumn<int>("delayMs");
        QTest::addColumn<int>("jitterMs");
        QTest::addColumn<bool>("prediction");
        QTest::newRow("rtt 100-150 ms") << 50 << 25 << true;
        QTest::newRow("rtt 200-300 ms") << 100 << 50 << true;
        QTest::newRow("rtt 200-300 ms, no block prediction") << 100 << 50 << false;
    }

    void miningAndPlacingWithLatency()
    {
        QFETCH(int, delayMs);
        QFETCH(int, jitterMs);
        QFETCH(bool, prediction);
        start(delayMs, jitterMs);
        m_session->setBlockPrediction(prediction);
        const int roundTripMs = 2 * (delayMs + jitterMs);
        const auto settled = [this] { return m_session->worldMirror().blocks().predictedCount() == 0; };

        // Placing: with block prediction the block shows at once and the server confirms it after a
        // round trip (decision E-853); without, it shows once the server's answer is back.
        m_session->sendChat(u"/give dirt 4"_s);
        QTRY_VERIFY_WITH_TIMEOUT(!m_session->inventory().selectedStack().isEmpty(), timeoutMs);
        // On the ground a little ahead.
        QcPlayerInput look;
        look.pitch = -50.0f;
        m_session->setIntent(look);
        QTRY_VERIFY_WITH_TIMEOUT(m_session->hand().target().has_value() && m_session->hand().target()->face == QcBlockFace::Up, timeoutMs);
        const QcBlockPos target = m_session->hand().target()->block.offset(0, 1, 0);
        QElapsedTimer timer;
        timer.start();
        QVERIFY(m_session->useItem());
        const QcBlockId dirt = *m_content->blockId(u"qtcraft:dirt"_s);
        if (prediction) {
            QCOMPARE(m_session->world()->blockAt(target), std::optional<QcBlockId>(dirt));
            QVERIFY(!settled());
        }
        QTRY_COMPARE_WITH_TIMEOUT(m_session->world()->blockAt(target), std::optional<QcBlockId>(dirt), timeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(settled(), timeoutMs);
        qInfo("placed and confirmed after %lld ms", static_cast<qlonglong>(timer.elapsed()));
        QVERIFY(timer.elapsed() >= qint64(2) * delayMs);
        QVERIFY(timer.elapsed() <= roundTripMs + 250);
        QCOMPARE(m_session->world()->blockAt(target), std::optional<QcBlockId>(dirt));

        m_session->setIntent({});
        QTest::qWait(roundTripMs);

        // Grass and dirt below (with prediction shown at the end of mining, decision E-853): each
        // shown within its break time plus a round trip and two ticks, then confirmed as air, never
        // refused and mined again.
        for (int i = 0; i < 4; ++i) {
            const QcBlockPos below = blockOf(m_session->playerMotion().position).offset(0, -1, 0);
            const double expected = QcMining::breakSeconds(m_content->block(*m_session->world()->blockAt(below)), nullptr);
            const double took = mineDown();
            QVERIFY(took >= 0.0);
            QVERIFY2(took <= expected + (roundTripMs + 250) / 1000.0, qPrintable(u"%1 s for a %2 s block"_s.arg(took).arg(expected)));
            QTRY_VERIFY_WITH_TIMEOUT(settled(), timeoutMs);
            QCOMPARE(m_session->world()->blockAt(below), std::optional<QcBlockId>(qcAirBlockId));
            QTRY_VERIFY_WITH_TIMEOUT(m_session->playerMotion().onGround, timeoutMs);
            QTest::qWait(roundTripMs);
        }
    }

    void roundTripIsMeasured_data()
    {
        QTest::addColumn<int>("delayMs");
        QTest::addColumn<int>("jitterMs");
        QTest::newRow("single player") << -1 << 0;
        QTest::newRow("rtt 200 ms") << 100 << 0;
        QTest::newRow("rtt 200-300 ms") << 100 << 50;
    }

    // The latency probes (decision E-886) measure the delayed link: the round trip lies between twice
    // the delay and twice the delay with jitter (plus the time the threads take to answer), and the
    // server lists it with the player. The rounds of the busy start are forgotten; under the load
    // of parallel tests single probes may still be late, so only the smallest one is held to the
    // tight bound.
    void roundTripIsMeasured()
    {
        QFETCH(int, delayMs);
        QFETCH(int, jitterMs);
        start(delayMs, jitterMs);
        m_session->resetLatency();

        const double lowMs = 2.0 * std::max(delayMs, 0) - 2.0;
        const double highMs = 2.0 * (std::max(delayMs, 0) + jitterMs) + 150.0;
        const double loadedMs = highMs + 1000.0;
        QTRY_VERIFY_WITH_TIMEOUT(m_session->latency() && m_session->latency()->samples >= 8, timeoutMs);
        const QcRoundTripSummary latency = *m_session->latency();
        qInfo("round trip %.2f ms (last %.2f, min %.2f, max %.2f, jitter %.2f, %d samples)", latency.smoothedMs, latency.lastMs, latency.minMs,
              latency.maxMs, latency.jitterMs, latency.samples);
        QVERIFY2(latency.minMs >= lowMs && latency.minMs <= highMs, qPrintable(u"min %1 ms"_s.arg(latency.minMs)));
        QVERIFY2(latency.maxMs <= loadedMs, qPrintable(u"max %1 ms"_s.arg(latency.maxMs)));
        QVERIFY2(latency.smoothedMs >= lowMs && latency.smoothedMs <= loadedMs, qPrintable(u"smoothed %1 ms"_s.arg(latency.smoothedMs)));
        if (jitterMs > 0) {
            QVERIFY2(latency.jitterMs > 0.0, qPrintable(u"jitter %1 ms"_s.arg(latency.jitterMs)));
        }

        // The server lists the round trip the client reported with the player.
        const auto listed = [&]() -> qint32 {
            const QList<QcMsg::PlayerList::Entry> players = m_session->players();
            const auto own = std::ranges::find(players, m_session->welcome().playerEntityId, &QcMsg::PlayerList::Entry::entityId);
            return own == players.end() ? -1 : own->latencyUs;
        };
        // A value of the busy start stays listed until a report differs visibly from it.
        QTRY_VERIFY2_WITH_TIMEOUT(listed() >= lowMs * 1000.0 && listed() <= loadedMs * 1000.0, qPrintable(u"listed %1 us"_s.arg(listed())), timeoutMs);
    }
};

QTEST_GUILESS_MAIN(TestLatency)
#include "test_latency.moc"
