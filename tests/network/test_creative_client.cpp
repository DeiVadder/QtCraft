#include "QcBaseContent.h"
#include "QcClientGameMode.h"
#include "QcClientSession.h"
#include "QcFlightLatch.h"
#include "QcLatencyTransport.h"
#include "QcLocalServerHost.h"
#include "QcMining.h"
#include "QcPhysics.h"
#include "QcWorldGenerators.h"
#include "QcWorldStorage.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 30'000;

std::shared_ptr<const QcContentRegistry> loadContent()
{
    QStringList warnings;
    return *QcBaseContent::load(warnings);
}

} // namespace

// The client side of the creative mode (decisions E-282 to E-284): the double tap that toggles
// flight, the pace of instant breaking, and a client session that flies, breaks and takes items
// from the catalogue over a link with latency without any correction by the server; a saved
// flight goes on after rejoining.
class TestCreativeClient : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = loadContent();
    std::unique_ptr<QcLocalServerHost> m_host;
    std::unique_ptr<QcClientSession> m_session;
    // Outlives the host, which releases the world's lock file on shutdown.
    QTemporaryDir m_directory;

    void start(QcGameMode mode, int delayMs, int jitterMs, const QString &worldDirectory = {})
    {
        QcServerConfig config;
        config.viewDistance = 2;
        config.seed = 1;
        config.generator = QString::fromLatin1(QcWorldGenerators::flat);
        config.defaultGameMode = mode;
        config.worldDirectory = worldDirectory;
        m_host = std::make_unique<QcLocalServerHost>(config, m_content, QcWorldGenerators::create(config.generator, m_content, 1, nullptr));
        QVERIFY(m_host->startError().isEmpty());
        connectSession(delayMs, jitterMs);
        QTRY_VERIFY_WITH_TIMEOUT(m_session->playerMotion().onGround, timeoutMs);
        QCOMPARE(m_session->gameMode().mode(), mode);
        QTest::qWait(2 * (delayMs + jitterMs) + 200);
        m_session->resetCorrections();
    }

    // A new client session over a link with latency, waiting until it plays.
    void connectSession(int delayMs, int jitterMs)
    {
        auto link = std::make_unique<QcLatencyTransport>(m_host->connectClient(), delayMs, jitterMs, quint32(delayMs * 17 + jitterMs));
        m_session = std::make_unique<QcClientSession>(std::move(link), u"Player"_s);
        m_session->start();
        QTRY_VERIFY_WITH_TIMEOUT(m_session->state() == QcClientSession::State::Playing, timeoutMs);
    }

    void takeOff()
    {
        m_session->gameMode().toggleFlight();
        drive(20, [](int) {
            QcPlayerInput up;
            up.jump = true;
            return up;
        });
    }

    // Runs `ticks` client ticks with an intent made per tick.
    template<typename Intent>
    void drive(int ticks, Intent intent)
    {
        int tick = 0;
        QMetaObject::Connection connection = connect(m_session.get(), &QcClientSession::ticked, this, [&] {
            m_session->setIntent(intent(tick++));
        });
        [[maybe_unused]] const bool finished = QTest::qWaitFor([&] { return tick >= ticks; }, timeoutMs);
        disconnect(connection);
        m_session->setIntent({});
    }

private slots:
    void cleanup()
    {
        if (m_session) {
            m_session->disconnectFromServer();
            m_session.reset();
        }
        if (m_host) {
            QVERIFY(m_host->shutdown().has_value());
            m_host.reset();
        }
    }

    void doubleTapsOfJumpToggle()
    {
        QcFlightLatch latch;
        QVERIFY(!latch.jumpPressed(1000));
        QVERIFY(latch.jumpPressed(1250));
        // The press that completed a pair starts no new one.
        QVERIFY(!latch.jumpPressed(1400));
        QVERIFY(latch.jumpPressed(1700));

        QVERIFY(!latch.jumpPressed(3000));
        QVERIFY(!latch.jumpPressed(3301));
        latch.reset();
        QVERIFY(!latch.jumpPressed(3400));
    }

    void flightIsOnlyWantedWhereTheModeAllowsIt()
    {
        QcClientGameMode mode;
        mode.toggleFlight();
        QVERIFY(!mode.flightWanted());

        mode.setMode(QcGameMode::Creative);
        mode.toggleFlight();
        QVERIFY(mode.flightWanted());
        mode.stepped(false);
        QVERIFY(!mode.flightWanted());

        mode.setFlightWanted(true);
        mode.setMode(QcGameMode::Survival);
        QVERIFY(!mode.flightWanted());
    }

    void instantBreakingKeepsItsPace()
    {
        QcClientGameMode mode;
        const QcBlockPos pos{1, 2, 3};
        QList<int> breaks;
        for (int tick = 0; tick < 12; ++tick) {
            if (mode.nextInstantBreak(pos)) {
                breaks.append(tick);
            }
        }
        QCOMPARE(breaks, (QList<int>{0, 5, 10}));

        // Letting go and clicking again breaks at once.
        QVERIFY(!mode.nextInstantBreak(std::nullopt));
        QCOMPARE(mode.nextInstantBreak(pos), pos);
    }

    void flyingNeedsNoCorrection_data()
    {
        QTest::addColumn<int>("delayMs");
        QTest::addColumn<int>("jitterMs");
        QTest::newRow("rtt 50-70 ms") << 25 << 10;
        QTest::newRow("rtt 200-300 ms") << 100 << 50;
    }

    void flyingNeedsNoCorrection()
    {
        QFETCH(int, delayMs);
        QFETCH(int, jitterMs);
        start(QcGameMode::Creative, delayMs, jitterMs);
        const double ground = m_session->playerMotion().position.y;

        // Take off, rise, sprint-fly in a curve, hover, descend and land.
        takeOff();
        QVERIFY(m_session->playerMotion().flying);
        QVERIFY(m_session->playerMotion().position.y > ground + 3.0);

        drive(40, [](int tick) {
            QcPlayerInput fast;
            fast.forward = 1.0f;
            fast.sprint = true;
            fast.yaw = float(tick * 3);
            return fast;
        });
        // Descending by sneaking does not crouch the figure (E-284).
        drive(5, [](int) {
            QcPlayerInput down;
            down.sneak = true;
            return down;
        });
        QVERIFY(m_session->playerMotion().flying);
        QVERIFY(!m_session->localPose(0.0).sneaking);
        drive(60, [](int) {
            QcPlayerInput down;
            down.sneak = true;
            return down;
        });

        QVERIFY(!m_session->playerMotion().flying);
        QVERIFY(m_session->playerMotion().onGround);
        // The answers to the last inputs arrive.
        QTest::qWait(2 * (delayMs + jitterMs) + 200);
        QVERIFY2(m_session->largestCorrection() < 1e-9, qPrintable(QString::number(m_session->largestCorrection())));
    }

    void savedFlightGoesOnAfterRejoining()
    {
        const QString directory = m_directory.path();
        start(QcGameMode::Creative, 25, 10, directory);
        takeOff();
        QVERIFY(m_session->playerMotion().flying);
        // Hover until the climb has died away and the answers to the last inputs arrived.
        drive(20, [](int) { return QcPlayerInput{}; });
        QTest::qWait(300);
        const double height = m_session->playerMotion().position.y;

        m_session->disconnectFromServer();
        const QString playerFile = QDir(directory).filePath(u"players/"_s + QcWorldStorage::playerFileName(u"Player"_s));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(playerFile), timeoutMs);
        connectSession(25, 10);
        QVERIFY(m_session->gameMode().flightWanted());
        // The first state replaces the empty prediction.
        m_session->resetCorrections();

        // Hovering on: neither the prediction nor the server lets the player fall.
        drive(20, [](int) { return QcPlayerInput{}; });
        QTest::qWait(300);
        QVERIFY(m_session->playerMotion().flying);
        // (A fall would cover metres in this time.)
        QVERIFY(std::abs(m_session->playerMotion().position.y - height) < 1e-3);
        QVERIFY2(m_session->largestCorrection() < 1e-9, qPrintable(QString::number(m_session->largestCorrection())));

        // Dying ends the wish: a player comes back on the ground.
        m_session->sendChat(u"/kill"_s);
        QTRY_VERIFY_WITH_TIMEOUT(m_session->isDead(), timeoutMs);
        QVERIFY(!m_session->gameMode().flightWanted());
    }

    void catalogueItemsArriveOverALaggingLink()
    {
        start(QcGameMode::Creative, 100, 50);
        const QcItemId diamond = *m_content->itemId(u"qtcraft:diamond"_s);
        m_session->gameMode().creativeItem(3, {diamond, 64, 0});
        QTRY_COMPARE_WITH_TIMEOUT(m_session->inventory().slot(3), (QcItemStack{diamond, 64, 0}), timeoutMs);

        // The bin: an empty stack clears the slot.
        m_session->gameMode().creativeItem(3, {});
        QTRY_VERIFY_WITH_TIMEOUT(m_session->inventory().slot(3).isEmpty(), timeoutMs);
        QVERIFY(m_session->largestCorrection() < 1e-9);
    }

    void survivalCannotTakeOff()
    {
        start(QcGameMode::Survival, 25, 10);
        takeOff();
        QVERIFY(!m_session->playerMotion().flying);
        QVERIFY(m_session->largestCorrection() < 1e-9);
    }

    void creativeBreaksWithoutWaiting()
    {
        start(QcGameMode::Creative, 50, 25);
        const QcBlockPos below = QcPhysics::blockContaining(m_session->playerMotion().position).offset(0, -1, 0);
        QcPlayerInput look;
        look.pitch = -90.0f;
        m_session->setIntent(look);

        const QcContentRegistry &content = m_session->world()->content();
        const QcBlockDefinition &ground = content.block(m_session->world()->blockAt(below).value_or(qcAirBlockId));
        const double survivalTicks = QcMining::breakSeconds(ground, nullptr) / QcPhysics::tickDuration;
        QVERIFY(survivalTicks >= 10.0);

        // Counted in client ticks, so a slow run (sanitizers) does not count against it.
        const quint64 startTick = m_session->tickCount();
        m_session->setMining(true);
        QTRY_VERIFY_WITH_TIMEOUT(m_session->world()->blockAt(below) == std::optional<QcBlockId>(qcAirBlockId), timeoutMs);
        m_session->setMining(false);
        const quint64 ticks = m_session->tickCount() - startTick;

        // A round trip of at most 150 ms (3 ticks) and a tick or two, well below what the block
        // takes by hand in survival.
        QVERIFY2(double(ticks) < survivalTicks / 2.0, qPrintable(u"%1 of %2"_s.arg(ticks).arg(survivalTicks)));
    }
};

QTEST_MAIN(TestCreativeClient)
#include "test_creative_client.moc"
