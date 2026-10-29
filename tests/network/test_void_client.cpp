#include "clientcore/session/QcClientSession.h"
#include "common/core/QcWorldConstants.h"
#include "common/physics/QcPhysics.h"
#include "QcLatencyTransport.h"
#include "QcTestUtil.h"
#include "server/core/QcServerConfig.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worlds/QcLocalServerHost.h"

#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 30'000;

} // namespace

// A client session falling or flying into the void below the world over a link with latency
// (decision E-288): the prediction and the server agree all the way down (no correction), the
// void kills with its message, and the player respawns at the spawn.
class TestVoidClient : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    std::unique_ptr<QcLocalServerHost> m_host;
    std::unique_ptr<QcClientSession> m_session;
    QStringList m_chat;

    void start(QcGameMode mode, int delayMs, int jitterMs)
    {
        QcServerConfig config;
        config.viewDistance = 2;
        config.seed = 1;
        config.generator = QString::fromLatin1(QcWorldGenerators::flat);
        config.defaultGameMode = mode;
        m_host = std::make_unique<QcLocalServerHost>(config, m_content, QcWorldGenerators::create(config.generator, m_content, 1, {}, QcGeneratorVersion::current));
        QVERIFY(m_host->startError().isEmpty());

        auto link = std::make_unique<QcLatencyTransport>(m_host->connectClient(), delayMs, jitterMs, static_cast<quint32>(delayMs * 17 + jitterMs));
        m_session = std::make_unique<QcClientSession>(std::move(link), u"Player"_s);
        connect(m_session.get(), &QcClientSession::chatReceived, this, [this](const QString &text) { m_chat.append(text); });
        m_session->start();
        QTRY_VERIFY_WITH_TIMEOUT(m_session->state() == QcClientSession::State::Playing, timeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(m_session->playerMotion().onGround, timeoutMs);
        QTest::qWait(2 * (delayMs + jitterMs) + 200);
        m_session->resetCorrections();
    }

    // Opens a 3 x 3 shaft down to the bottom of the world, centred `east` blocks east of the
    // player, and waits until the client knows of it. Removing the ground under a standing player
    // would itself need a correction (the server lets it fall a round trip earlier), so a
    // standing player walks into a shaft beside it.
    void openHole(int east)
    {
        const QcBlockPos feet = QcPhysics::blockContaining(m_session->playerMotion().position);
        const QcBlockPos centre = feet.offset(east, 0, 0);
        m_session->sendChat(u"/fill %1 0 %2 %3 %4 %5 air"_s.arg(centre.x - 1).arg(centre.z - 1).arg(centre.x + 1).arg(centre.y - 1).arg(centre.z + 1));
        QTRY_COMPARE_WITH_TIMEOUT(m_session->world()->blockAt({centre.x, 0, centre.z}), std::optional<QcBlockId>(qcAirBlockId), timeoutMs);
    }

    // Walks east until the player drops into the shaft, then lets go.
    void walkIntoHole()
    {
        const double ground = m_session->playerMotion().position.y;
        const bool falling = QTest::qWaitFor([&] {
            QcPlayerInput east;
            east.yaw = 90.0f;
            east.forward = 1.0f;
            m_session->setIntent(east);
            return m_session->playerMotion().position.y < ground - 0.5;
        }, timeoutMs);
        m_session->setIntent({});
        QVERIFY(falling);
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

    // Waits for the first void damage, checks that nothing was corrected on the way down, then
    // for the death, its message and the respawn.
    void expectDeathInTheVoid(int delayMs, int jitterMs)
    {
        QTRY_VERIFY_WITH_TIMEOUT(m_session->vitals().health() < m_session->vitals().maxHealth(), timeoutMs);
        QVERIFY(QcWorld::isInVoid(m_session->playerMotion().position.y));
        // The answers to the inputs up to the first hit arrive; the player dies only 2 s later.
        QTest::qWait(2 * (delayMs + jitterMs) + 200);
        QVERIFY(!m_session->isDead());
        QVERIFY2(m_session->prediction().largestCorrection() < 1e-9, qPrintable(QString::number(m_session->prediction().largestCorrection())));

        QTRY_VERIFY_WITH_TIMEOUT(m_session->isDead(), timeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(m_chat.contains(u"Player fell out of the world"_s), timeoutMs);

        m_session->respawn();
        QTRY_VERIFY_WITH_TIMEOUT(!m_session->isDead() && m_session->playerMotion().position.y > 0.0, timeoutMs);
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
        m_chat.clear();
    }

    void fallingIntoTheVoidNeedsNoCorrection_data()
    {
        QTest::addColumn<int>("delayMs");
        QTest::addColumn<int>("jitterMs");
        QTest::newRow("rtt 50-70 ms") << 25 << 10;
        QTest::newRow("rtt 200-300 ms") << 100 << 50;
    }

    void fallingIntoTheVoidNeedsNoCorrection()
    {
        QFETCH(int, delayMs);
        QFETCH(int, jitterMs);
        start(QcGameMode::Survival, delayMs, jitterMs);
        openHole(3);
        walkIntoHole();
        expectDeathInTheVoid(delayMs, jitterMs);
    }

    void flyingDownIntoTheVoidNeedsNoCorrection()
    {
        start(QcGameMode::Creative, 50, 25);

        // Take off, open the shaft under the hovering player (nothing changes for a flyer), then
        // descend through it past the bottom of the world; flight holds the player there until the
        // void kills it.
        m_session->gameMode().toggleFlight();
        drive(10, [](int) {
            QcPlayerInput up;
            up.jump = true;
            return up;
        });
        QVERIFY(m_session->playerMotion().flying);
        openHole(0);
        const bool down = QTest::qWaitFor([this] {
            QcPlayerInput descend;
            descend.sneak = true;
            m_session->setIntent(descend);
            return QcWorld::isInVoid(m_session->playerMotion().position.y + 2.0);
        }, timeoutMs);
        m_session->setIntent({});
        QVERIFY(down);
        QVERIFY(m_session->playerMotion().flying);
        expectDeathInTheVoid(50, 25);
    }
};

QTEST_MAIN(TestVoidClient)
#include "test_void_client.moc"
