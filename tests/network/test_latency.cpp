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
        auto link = std::make_unique<QcLatencyTransport>(m_host->connectClient(), delayMs, jitterMs, quint32(delayMs * 31 + jitterMs));
        m_link = link.get();
        m_session = std::make_unique<QcClientSession>(std::move(link), u"Player"_s);
        m_session->start();
        QTRY_VERIFY_WITH_TIMEOUT(m_session->state() == QcClientSession::State::Playing && m_session->playerMotion().onGround, timeoutMs);
        // Settle: the first states move the player from the origin to the spawn.
        QTest::qWait(2 * (delayMs + jitterMs) + 200);
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
        m_lastMsPerTick = tick > 0 ? double(elapsed.elapsed()) / tick : 0.0;
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
        qInfo("mined %s in %lld ms", qPrintable(m_content->block(block).name), qlonglong(timer.elapsed()));
        return double(timer.elapsed()) / 1000.0;
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
            input.yaw = float(tick * 3 % 360);
            return input;
        });
        QTest::qWait(2 * (delayMs + jitterMs) + 200);

        // Every server state matched the prediction it answered: no correction, so no jump on screen.
        qInfo("largest correction %.6f m, most pending inputs %lld, largest one-way delay %lld ms", m_session->prediction().largestCorrection(),
              qlonglong(mostPending), qlonglong(m_link->largestDelayMs()));
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
        QTest::newRow("rtt 100-150 ms") << 50 << 25;
        QTest::newRow("rtt 200-300 ms") << 100 << 50;
    }

    void miningAndPlacingWithLatency()
    {
        QFETCH(int, delayMs);
        QFETCH(int, jitterMs);
        start(delayMs, jitterMs);
        const int roundTripMs = 2 * (delayMs + jitterMs);

        // Placing: the block shows once the server's answer is back.
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
        QTRY_COMPARE_WITH_TIMEOUT(m_session->world()->blockAt(target), std::optional<QcBlockId>(dirt), timeoutMs);
        qInfo("placed after %lld ms", qlonglong(timer.elapsed()));
        QVERIFY(timer.elapsed() >= qint64(2) * delayMs);
        QVERIFY(timer.elapsed() <= roundTripMs + 250);

        m_session->setIntent({});
        QTest::qWait(roundTripMs);

        // Grass and dirt below: each confirmed within its break time plus a round trip and two ticks,
        // never refused and mined again.
        for (int i = 0; i < 4; ++i) {
            const QcBlockPos below = blockOf(m_session->playerMotion().position).offset(0, -1, 0);
            const double expected = QcMining::breakSeconds(m_content->block(*m_session->world()->blockAt(below)), nullptr);
            const double took = mineDown();
            QVERIFY(took >= 0.0);
            QVERIFY2(took <= expected + (roundTripMs + 250) / 1000.0, qPrintable(u"%1 s for a %2 s block"_s.arg(took).arg(expected)));
            QTRY_VERIFY_WITH_TIMEOUT(m_session->playerMotion().onGround, timeoutMs);
            QTest::qWait(roundTripMs);
        }
    }
};

QTEST_GUILESS_MAIN(TestLatency)
#include "test_latency.moc"
