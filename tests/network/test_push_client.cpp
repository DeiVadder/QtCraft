#include "clientcore/session/QcClientSession.h"
#include "common/physics/QcPhysics.h"
#include "QcLatencyTransport.h"
#include "QcTestUtil.h"
#include "server/core/QcServerConfig.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worlds/QcLocalServerHost.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <optional>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 30'000;
constexpr int delayMs = 100;
constexpr int jitterMs = 50;
// Long enough for every answer to arrive across the link.
constexpr int settleMs = 2 * (delayMs + jitterMs) + 300;

} // namespace

// The prediction of a player pushed by a creature (decision E-882): the client pushes its player with
// the creatures as the server last sent them, moved on along their last step by the lead of its
// input (QcPushBodies), the push the server gives him with its creatures between two ticks. Across
// a link with a round trip of 200 to 300 ms, walking past a cow that stands still turns the player
// aside without any correction, and pushing a walking cow along costs far less correction than
// without moving it on.
class TestPushClient : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    std::unique_ptr<QcLocalServerHost> m_host;
    std::unique_ptr<QcClientSession> m_session;
    int m_ticks = 0;
    // Ticks in which the player touched the cow (chaseACow).
    int m_touching = 0;

    void start()
    {
        QcServerConfig config;
        config.viewDistance = 2;
        config.seed = 1;
        config.generator = QString::fromLatin1(QcWorldGenerators::flat);
        m_host = std::make_unique<QcLocalServerHost>(config, m_content, QcWorldGenerators::create(config.generator, m_content, 1, {}, QcGeneratorVersion::current));
        QVERIFY(m_host->startError().isEmpty());

        auto link = std::make_unique<QcLatencyTransport>(m_host->connectClient(), delayMs, jitterMs, static_cast<quint32>(delayMs * 13 + jitterMs));
        m_session = std::make_unique<QcClientSession>(std::move(link), u"Player"_s);
        m_session->start();
        QTRY_VERIFY_WITH_TIMEOUT(m_session->state() == QcClientSession::State::Playing, timeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(m_session->playerMotion().onGround, timeoutMs);
    }

    // Runs `ticks` client ticks with `intent`.
    void drive(int ticks, const QcPlayerInput &intent)
    {
        // The intent holds until changed; the session's ticks are counted.
        m_ticks = 0;
        m_session->setIntent(intent);
        QMetaObject::Connection connection = connect(m_session.get(), &QcClientSession::ticked, this, [this] { ++m_ticks; });
        const bool finished = QTest::qWaitFor([&] { return m_ticks >= ticks; }, timeoutMs);
        disconnect(connection);
        m_session->setIntent({});
        QVERIFY2(finished, qPrintable(u"%1 of %2 ticks"_s.arg(m_ticks).arg(ticks)));
    }

    // The creature the client knows, if there is one.
    std::optional<QcVec3> creature() const
    {
        for (const QcClientEntity &entity : m_session->entities()) {
            if (entity.isCreature()) {
                return entity.position;
            }
        }
        return std::nullopt;
    }

    // What a chase of a cow showed: the largest correction of the prediction and the ticks in which
    // the player touched the cow.
    struct Chase
    {
        double largestCorrection = 0.0;
        int touching = 0;
    };

    // Starts a server with one cow with a mind ahead of the player and follows it for `ticks`
    // ticks, walking into it; stops the server again.
    Chase chaseACow(int ticks)
    {
        Chase chase;
        start();
        if (QTest::currentTestFailed()) {
            return chase;
        }
        m_session->sendChat(u"/gamerule mobSpawning false"_s);
        m_session->sendChat(u"/remove mobs world"_s);
        drive(2, QcPlayerInput{.yaw = 90.0f});
        m_session->sendChat(u"/summon cow 1"_s);
        // Returns a value, so no QVERIFY here: a failure is reported with QTest::qFail.
        if (!QTest::qWaitFor([this] { return creature().has_value(); }, timeoutMs)) {
            QTest::qFail("the cow never appeared", __FILE__, __LINE__);
            return chase;
        }
        QTest::qWait(settleMs + 500);
        m_session->resetCorrections();

        const double reach = (QcPhysics::playerWidth + m_content->mobs()[m_content->mobIndex(u"qtcraft:cow"_s).value_or(0)].width) * 0.5;
        m_ticks = 0;
        // Counted in a member: the connection's lambda captures no local by reference.
        m_touching = 0;
        QMetaObject::Connection connection = connect(m_session.get(), &QcClientSession::ticked, this, [this, reach] {
            ++m_ticks;
            const QcVec3 player = m_session->playerMotion().position;
            const QcVec3 cow = creature().value_or(player);
            const double dx = cow.x - player.x;
            const double dz = cow.z - player.z;
            m_touching += std::hypot(dx, dz) < reach ? 1 : 0;
            // Walks at the cow, which the player's push drives on at its walking speed.
            m_session->setIntent(walking(static_cast<float>(qRadiansToDegrees(std::atan2(dx, -dz)))));
        });
        m_session->setIntent(walking(90.0f));
        const bool finished = QTest::qWaitFor([&] { return m_ticks >= ticks; }, timeoutMs);
        disconnect(connection);
        m_session->setIntent({});
        if (!finished) {
            QTest::qFail(qPrintable(u"%1 of %2 ticks"_s.arg(m_ticks).arg(ticks)), __FILE__, __LINE__);
            return chase;
        }
        QTest::qWait(settleMs);
        chase.largestCorrection = m_session->prediction().largestCorrection();
        chase.touching = m_touching;
        cleanup();
        return chase;
    }

    static QcPlayerInput walking(float yaw)
    {
        QcPlayerInput input;
        input.yaw = yaw;
        input.forward = 1.0f;
        return input;
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

    void walkingPastAStandingCowNeedsNoCorrection()
    {
        start();
        // Nobody else around: no animals of the chunks, no spawning.
        m_session->sendChat(u"/gamerule mobSpawning false"_s);
        m_session->sendChat(u"/remove mobs world"_s);
        // Facing east, a cow without a mind appears three metres ahead and lands.
        drive(2, QcPlayerInput{.yaw = 90.0f});
        m_session->sendChat(u"/summon cow 1 noai"_s);
        const auto creatures = [this] {
            return static_cast<int>(std::ranges::count_if(m_session->entities(), [](const QcClientEntity &entity) { return entity.isCreature(); }));
        };
        QTRY_COMPARE_WITH_TIMEOUT(creatures(), 1, timeoutMs);
        QTest::qWait(settleMs + 500);

        // A step to the north, so the way east passes the cow a little to its north.
        const double southOfTheWay = m_session->playerMotion().position.z;
        drive(1, walking(0.0f));
        const double step = southOfTheWay - m_session->playerMotion().position.z;
        QVERIFY2(step > 0.1 && step < 0.6, qPrintable(QString::number(step)));
        QTest::qWait(settleMs);
        m_session->resetCorrections();
        const QcVec3 before = m_session->playerMotion().position;

        drive(30, walking(90.0f));
        const QcVec3 after = m_session->playerMotion().position;
        if (QTest::currentTestFailed()) {
            return;
        }
        // Past the cow and turned aside to the north by it.
        QVERIFY2(after.x - before.x > 4.0, qPrintable(QString::number(after.x - before.x)));
        QVERIFY2(after.z < before.z - 0.05, qPrintable(QString::number(after.z - before.z)));

        QTest::qWait(settleMs);
        QVERIFY2(m_session->prediction().largestCorrection() < 1e-9, qPrintable(QString::number(m_session->prediction().largestCorrection())));
    }

    void walkingIntoAWalkingCowCostsLittleCorrection()
    {
        // The client moves the creatures on along their last step by the lead of its input
        // (QcPushBodies); without that it pushes with a cow a round trip behind the server's.
        constexpr int ticks = 80;
        const Chase moved = chaseACow(ticks);
        if (QTest::currentTestFailed()) {
            return;
        }
        qputenv("QTCRAFT_PUSH_EXTRAPOLATION", "0");
        const Chase behind = chaseACow(ticks);
        qunsetenv("QTCRAFT_PUSH_EXTRAPOLATION");
        if (QTest::currentTestFailed()) {
            return;
        }
        qInfo("largest correction %.4f m with the creatures moved on (%d ticks touching), %.4f m without (%d ticks touching)",
              moved.largestCorrection, moved.touching, behind.largestCorrection, behind.touching);

        // The player pushed the cow along for most of the way, both times.
        QVERIFY2(moved.touching > ticks / 2, qPrintable(QString::number(moved.touching)));
        QVERIFY2(behind.touching > ticks / 2, qPrintable(QString::number(behind.touching)));
        // Measured (2026-10-24, 200-300 ms round trip): 1.8-3 cm moved on, 21-34 cm without.
        QVERIFY2(moved.largestCorrection < 0.06, qPrintable(QString::number(moved.largestCorrection)));
        QVERIFY2(moved.largestCorrection < behind.largestCorrection * 0.4,
                 qPrintable(u"%1 %2"_s.arg(moved.largestCorrection).arg(behind.largestCorrection)));
    }
};

QTEST_MAIN(TestPushClient)
#include "test_push_client.moc"
