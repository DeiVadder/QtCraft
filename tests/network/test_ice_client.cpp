#include "clientcore/session/QcClientSession.h"
#include "common/content/registry/QcContentSerializer.h"
#include "common/physics/QcPhysics.h"
#include "QcLatencyTransport.h"
#include "QcTestUtil.h"
#include "QcTestWorld.h"
#include "server/core/QcServerConfig.h"
#include "worldgen/generators/QcWorldGenerators.h"
#include "worlds/QcLocalServerHost.h"

#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 30'000;

// The content as the client gets it: written by the server and read back.
QcResult<QcContentRegistry> roundTrip(const QcContentRegistry &content)
{
    QByteArray bytes;
    {
        QDataStream out(&bytes, QIODevice::WriteOnly);
        QcContentSerializer::write(content, out);
    }
    QDataStream in(bytes);
    return QcContentSerializer::read(in);
}

// What the player does in tick `tick` of `skating`: walks east, lets go and slides, sprints west
// jumping now and then, lets go again.
QcPlayerInput skating(int tick)
{
    QcPlayerInput input;
    if (tick < 30) {
        input.yaw = 90.0f;
        input.forward = 1.0f;
    } else if (tick >= 70 && tick < 110) {
        input.yaw = 270.0f;
        input.forward = 1.0f;
        input.sprint = true;
        input.jump = tick % 12 == 0;
    }
    return input;
}
constexpr int skatingTicks = 150;

} // namespace

// The prediction of a client sliding on ice (decision E-430): the slipperiness reaches the client with
// the content, so the client's steps (QcEntityMotion::stepPlayer with the content it read) slide exactly
// as the server's do, tick by tick, on ice, packed ice and blue ice. Once across a link with latency
// in real time (blue ice): walking, sprinting, jumping and sliding needs no correction (no
// rubber-banding).
class TestIceClient : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    std::unique_ptr<QcLocalServerHost> m_host;
    std::unique_ptr<QcClientSession> m_session;

    void start(int delayMs, int jitterMs)
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

    // Lays `block` as the ground around the player and waits until the client knows of it.
    void layGround(const QString &block, int delayMs, int jitterMs)
    {
        const QcBlockPos feet = QcPhysics::blockContaining(m_session->playerMotion().position);
        const QcBlockPos ground = feet.offset(0, -1, 0);
        m_session->sendChat(u"/fill %1 %2 %3 %4 %2 %5 %6"_s.arg(ground.x - 12).arg(ground.y).arg(ground.z - 12).arg(ground.x + 12).arg(ground.z + 12).arg(block));
        const QcBlockId id = *m_content->blockId(u"qtcraft:"_s + block);
        QTRY_COMPARE_WITH_TIMEOUT(m_session->world()->blockAt(ground.offset(12, 0, 12)), std::optional<QcBlockId>(id), timeoutMs);
        QTest::qWait(2 * (delayMs + jitterMs) + 200);
        m_session->resetCorrections();
    }

    // Runs `ticks` client ticks with an intent made per tick.
    template<typename Intent>
    void drive(int ticks, Intent intent)
    {
        int tick = 0;
        QMetaObject::Connection connection = connect(m_session.get(), &QcClientSession::ticked, this, [&] {
            m_session->setIntent(intent(tick++));
        });
        const bool finished = QTest::qWaitFor([&] { return tick >= ticks; }, timeoutMs);
        disconnect(connection);
        m_session->setIntent({});
        QVERIFY2(finished, qPrintable(u"%1 of %2 ticks"_s.arg(tick).arg(ticks)));
    }

    // Walks east and back west, sprints and jumps, letting go in between: the player slides after
    // each; returns how far it slid after the first walk.
    double skate()
    {
        drive(30, skating);
        const double released = m_session->playerMotion().position.x;
        drive(40, [](int tick) { return skating(30 + tick); });
        const double slid = m_session->playerMotion().position.x - released;
        drive(80, [](int tick) { return skating(70 + tick); });
        return slid;
    }

    void expectNoCorrection(int delayMs, int jitterMs)
    {
        // The answers to every input arrive.
        QTest::qWait(2 * (delayMs + jitterMs) + 200);
        QVERIFY2(m_session->prediction().largestCorrection() < 1e-9, qPrintable(QString::number(m_session->prediction().largestCorrection())));
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

    void slipperinessTravelsWithTheContent()
    {
        const QcResult<QcContentRegistry> read = roundTrip(*m_content);
        QVERIFY2(read, qPrintable(read ? QString() : read.error()));
        QCOMPARE(read->blocks().size(), m_content->blocks().size());
        for (qsizetype i = 0; i < m_content->blocks().size(); ++i) {
            QCOMPARE(read->blocks()[i].slipperiness, m_content->blocks()[i].slipperiness);
        }
        QCOMPARE(read->block(*read->blockId(u"qtcraft:blue_ice"_s)).slipperiness, 0.989);
    }

    void thePredictionSlidesLikeTheServerTickByTick_data()
    {
        QTest::addColumn<QString>("block");
        QTest::newRow("ice") << u"qtcraft:ice"_s;
        QTest::newRow("packed ice") << u"qtcraft:packed_ice"_s;
        QTest::newRow("blue ice") << u"qtcraft:blue_ice"_s;
    }

    void thePredictionSlidesLikeTheServerTickByTick()
    {
        QFETCH(QString, block);
        const QcResult<QcContentRegistry> read = roundTrip(*m_content);
        QVERIFY(read);
        QcTestWorld server(*m_content);
        QcTestWorld client(*read);
        server.fillBox({-40, -1, -40}, {40, -1, 40}, *m_content->blockId(block));
        client.fillBox({-40, -1, -40}, {40, -1, 40}, *read->blockId(block));

        QcEntityMotion onServer;
        onServer.position = {0.5, 0.0, 0.5};
        onServer.onGround = true;
        QcEntityMotion onClient = onServer;
        double released = 0.0;
        double slid = 0.0;
        for (int tick = 0; tick < skatingTicks; ++tick) {
            onServer.stepPlayer(skating(tick), server, QcPhysics::tickDuration, QcMovement::Walk);
            onClient.stepPlayer(skating(tick), client, QcPhysics::tickDuration, QcMovement::Walk);
            QCOMPARE(onClient.position, onServer.position);
            QCOMPARE(onClient.velocity, onServer.velocity);
            QCOMPARE(onClient.onGround, onServer.onGround);
            released = tick == 29 ? onServer.position.x : released;
            slid = tick == 69 ? onServer.position.x - released : slid;
        }
        // It slid on after letting go (on ground that grips it would stop at once).
        QVERIFY2(slid > 0.5, qPrintable(QString::number(slid)));
    }

    void slidingOnIceNeedsNoCorrection()
    {
        // Real time across a link: one round on blue ice with a round trip of 200 to 300 ms.
        constexpr int delayMs = 100;
        constexpr int jitterMs = 50;
        start(delayMs, jitterMs);
        layGround(u"blue_ice"_s, delayMs, jitterMs);
        const double slid = skate();
        if (QTest::currentTestFailed()) {
            return;
        }
        QVERIFY2(slid > 1.0, qPrintable(QString::number(slid)));
        expectNoCorrection(delayMs, jitterMs);
    }
};

QTEST_MAIN(TestIceClient)
#include "test_ice_client.moc"
