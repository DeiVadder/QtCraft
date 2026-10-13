#include "QcBaseContent.h"
#include "QcClientEntities.h"
#include "QcClientSession.h"
#include "QcConnection.h"
#include "QcDebugSnapshot.h"
#include "QcInProcessTransport.h"
#include "QcLocalServerHost.h"
#include "QcPhysics.h"
#include "QcProtocol.h"
#include "QcRawClient.h"
#include "QcServer.h"
#include "QcTestTerrainGenerator.h"

#include <QSignalSpy>
#include <QTest>

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 20000;

std::shared_ptr<const QcContentRegistry> loadContent()
{
    QStringList warnings;
    auto content = QcBaseContent::load(warnings);
    if (!content) {
        qFatal("content failed: %s", qPrintable(content.error()));
    }
    return std::move(*content);
}

std::unique_ptr<QcLocalServerHost> makeHost(const std::shared_ptr<const QcContentRegistry> &content, int viewDistance = 2)
{
    QcServerConfig config;
    config.worldName = u"Test"_s;
    config.seed = 7;
    config.viewDistance = viewDistance;
    return std::make_unique<QcLocalServerHost>(config, content, std::make_unique<QcTestTerrainGenerator>(content));
}

QcBlockPos feetBlock(const QcEntityMotion &motion)
{
    return {int(std::floor(motion.position.x)), int(std::floor(motion.position.y)), int(std::floor(motion.position.z))};
}

} // namespace

class TestClientServer : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = loadContent();

private slots:
    void movingBlocksNeedAKnownBlock()
    {
        // A sliding block (decision E-337) is accepted only with a block the content knows; air or an
        // unknown id is an invalid message.
        QcClientEntities entities;
        QcMsg::EntitySpawn moving{9, QcMsg::EntitySpawn::movingBlockType, QString(), {0.5, 70.0, 0.5}, 0.0f, 0.0, {}, {}, 0.0f, 0, qcNoItem};
        moving.block = *m_content->blockId(u"qtcraft:stone"_s);
        QVERIFY(entities.spawn(moving, m_content.get()));
        QVERIFY(entities.all().value(9).isMovingBlock());
        QVERIFY(entities.all().value(9).isBlockEntity());
        moving.id = 10;
        moving.block = qcAirBlockId;
        QVERIFY(!entities.spawn(moving, m_content.get()));
        moving.block = QcBlockId(m_content->stateCount());
        QVERIFY(!entities.spawn(moving, m_content.get()));
    }

    void sessionReceivesWorldAndStandsOnGround()
    {
        auto host = makeHost(m_content, 2);
        QcClientSession session(host->connectClient(), u"Tester"_s);
        session.start();
        QTRY_VERIFY_WITH_TIMEOUT(session.state() == QcClientSession::State::Playing, timeoutMs);
        QCOMPARE(session.worldName(), u"Test"_s);
        QCOMPARE(session.seed(), 7);
        QVERIFY(session.content());
        QCOMPARE(session.content()->blockCount(), m_content->blockCount());

        // View distance 2 means a 5 x 5 square of chunks.
        QTRY_COMPARE_WITH_TIMEOUT(session.world() ? session.world()->chunkCount() : 0, 25, timeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(session.playerMotion().onGround, timeoutMs);

        const QcBlockPos feet = feetBlock(session.playerMotion());
        const QcContentRegistry &content = *session.content();
        const auto below = session.world()->blockAt(feet.offset(0, -1, 0));
        QVERIFY(below.has_value());
        QVERIFY(content.block(*below).solid);
        QVERIFY(!content.block(*session.world()->blockAt(feet)).solid);
        QVERIFY(!content.block(*session.world()->blockAt(feet.offset(0, 1, 0))).solid);
        QVERIFY(host->shutdown().has_value());
    }

    void theClientsDayFollowsTheServersTickRate()
    {
        // The session advances the time of day between two WorldTime messages by the server's speed
        // (decision E-249): twice as fast at 40 ticks per second, not at all while frozen.
        auto host = makeHost(m_content, 1);
        QcClientSession session(host->connectClient(), u"Tester"_s);
        session.start();
        QTRY_VERIFY_WITH_TIMEOUT(session.state() == QcClientSession::State::Playing, timeoutMs);

        session.sendChat(u"/tick rate 40"_s);
        QTRY_COMPARE_WITH_TIMEOUT(session.timeSpeed(), 2.0, timeoutMs);
        double before = session.worldTime();
        for (int i = 0; i < 10; ++i) {
            session.tick();
        }
        QCOMPARE(session.worldTime(), before + 10 * QcPhysics::tickDuration * 2.0);

        session.sendChat(u"/tick freeze"_s);
        QTRY_COMPARE_WITH_TIMEOUT(session.timeSpeed(), 0.0, timeoutMs);
        before = session.worldTime();
        for (int i = 0; i < 10; ++i) {
            session.tick();
        }
        QCOMPARE(session.worldTime(), before);
        QVERIFY(host->shutdown().has_value());
    }

    void clientMirrorMatchesServerGeneration()
    {
        auto host = makeHost(m_content, 1);
        QcClientSession session(host->connectClient(), u"Tester"_s);
        session.start();
        QTRY_COMPARE_WITH_TIMEOUT(session.world() ? session.world()->chunkCount() : 0, 9, timeoutMs);

        // The generator is deterministic, so a fresh instance yields the same blocks.
        const QcTestTerrainGenerator reference(m_content);
        const auto expected = reference.generate({1, -1});
        const auto received = session.world()->chunk({1, -1});
        QVERIFY(received);
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                for (int y = 0; y < QcWorld::worldHeight; ++y) {
                    QCOMPARE(received->block(x, y, z), expected->block(x, y, z));
                }
            }
        }
        QVERIFY(host->shutdown().has_value());
    }

    void predictionConvergesWithServer()
    {
        auto host = makeHost(m_content, 2);
        QcClientSession session(host->connectClient(), u"Tester"_s);
        session.start();
        QTRY_VERIFY_WITH_TIMEOUT(session.state() == QcClientSession::State::Playing && session.playerMotion().onGround, timeoutMs);
        const QcVec3 start = session.playerMotion().position;

        QcPlayerInput intent;
        intent.forward = 1.0f;
        intent.yaw = 180.0f;
        session.setIntent(intent);
        QSignalSpy ticks(&session, &QcClientSession::ticked);
        QTRY_VERIFY_WITH_TIMEOUT(ticks.count() >= 20, timeoutMs);
        session.setIntent({});

        // Once all inputs are acknowledged the predicted state equals the authoritative one.
        QTRY_VERIFY_WITH_TIMEOUT(session.pendingInputCount() <= 1 && session.playerMotion().onGround
                                     && std::abs(session.playerMotion().velocity.z) < 1e-9,
                                 timeoutMs);
        const QcVec3 end = session.playerMotion().position;

        // Facing south (+Z) the player moved forward without sideways drift.
        QVERIFY2(end.z - start.z > 1.0, qPrintable(QString::number(end.z - start.z)));
        QCOMPARE(end.x, start.x);
        QVERIFY(host->shutdown().has_value());
    }

    void protocolVersionMismatchIsRejected()
    {
        auto host = makeHost(m_content);
        QStringList reasons;
        QcConnection connection(host->connectClient(), QcProtocol::Peer::Server, [&reasons](QcMessage &&message) {
            if (const auto *disconnect = std::get_if<QcMsg::Disconnect>(&message)) {
                reasons.append(disconnect->reason);
            }
        });
        QSignalSpy closed(&connection, &QcConnection::closed);
        connection.send(QcMsg::Hello{QcProtocol::version + 1, u"Old"_s, {}});
        QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, timeoutMs);
        QCOMPARE(reasons.size(), 1);
        QVERIFY(reasons.constFirst().contains(u"version"_s));
        QVERIFY(host->shutdown().has_value());
    }

    void debugSubscriptionBaseStaysAndFeetChooseTheLight()
    {
        auto host = makeHost(m_content, 2);
        QcClientSession session(host->connectClient(), u"Tester"_s);
        session.setDebugSubscriptionBase(QcMsg::DebugSubscribe::stats);
        session.start();
        QTRY_VERIFY_WITH_TIMEOUT(session.state() == QcClientSession::State::Playing && session.serverDebug().latest(), timeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(session.playerMotion().onGround, timeoutMs);

        // Switching the requested part off keeps the base: the server data keeps coming.
        session.setDebugSubscription(QcMsg::DebugSubscribe::all);
        session.setDebugSubscription(0);
        QCOMPARE(session.debugSubscription(), QcMsg::DebugSubscribe::stats);
        const quint64 seen = session.serverDebug().latest()->tick;
        QTRY_VERIFY_WITH_TIMEOUT(session.serverDebug().latest() && session.serverDebug().latest()->tick > seen, timeoutMs);

        // The snapshot names light and block of the position it is given (the drawn one), here
        // three blocks down in the ground, where no sky light reaches.
        const QcVec3 below = session.playerMotion().position - QcVec3{0.0, 3.0, 0.0};
        const QcDebugSnapshot snapshot = QcDebugSnapshot::collect(session, below);
        QCOMPARE(snapshot.position, below);
        const QcBlockPos block = QcPhysics::blockContaining(below);
        QCOMPARE(snapshot.block(), block);
        QCOMPARE(snapshot.feetLight.sky, QcLightProbe(*session.world(), block).at(block).sky);
        QCOMPARE(snapshot.feetLight.sky, 0);
        QCOMPARE(QcDebugSnapshot::collect(session).feetLight.sky, 15);
        QVERIFY(host->shutdown().has_value());
    }

    void hostPlayerChatIsNotRateLimited()
    {
        // The client of the in-process host is the local player: a burst of commands far above the
        // chat limit (bursts of 20) arrives complete, as the benchmark scenarios need it.
        auto host = makeHost(m_content);
        int healed = 0;
        QcConnection connection(host->connectClient(), QcProtocol::Peer::Server, [&healed](QcMessage &&message) {
            if (const auto *chat = std::get_if<QcMsg::ChatMessage>(&message); chat && chat->key == u"Healed"_s) {
                ++healed;
            }
        });
        connection.send(QcMsg::Hello{QcProtocol::version, u"Owner"_s, {}});
        for (int i = 0; i < 100; ++i) {
            connection.send(QcMsg::ChatInput{u"/heal"_s});
        }
        QTRY_COMPARE_WITH_TIMEOUT(healed, 100, timeoutMs);
        QVERIFY(host->shutdown().has_value());
    }

    void messagesBeforeHelloAreRejected()
    {
        auto host = makeHost(m_content);
        QcConnection connection(host->connectClient(), QcProtocol::Peer::Server, [](QcMessage &&) {});
        QSignalSpy closed(&connection, &QcConnection::closed);
        connection.send(QcMsg::PlayerInput{});
        QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, timeoutMs);
        QVERIFY(host->shutdown().has_value());
    }

    void itemEntitiesAreMirroredAndBogusOnesRejected_data()
    {
        QTest::addColumn<qint32>("type");
        QTest::addColumn<quint32>("item");
        QTest::addColumn<int>("count");
        QTest::addColumn<bool>("accepted");
        const quint32 dirt = *loadContent()->itemId(u"qtcraft:dirt"_s);
        QTest::newRow("dirt") << QcMsg::EntitySpawn::itemType << dirt << 3 << true;
        QTest::newRow("unknown item") << QcMsg::EntitySpawn::itemType << 999999u << 3 << false;
        QTest::newRow("no item") << QcMsg::EntitySpawn::itemType << quint32(qcNoItem) << 3 << false;
        QTest::newRow("empty stack") << QcMsg::EntitySpawn::itemType << dirt << 0 << false;
        QTest::newRow("unknown type") << qint32(-3) << dirt << 3 << false;
    }

    void itemEntitiesAreMirroredAndBogusOnesRejected()
    {
        QFETCH(qint32, type);
        QFETCH(quint32, item);
        QFETCH(int, count);
        QFETCH(bool, accepted);

        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        QcConnection server(std::move(serverEnd), QcProtocol::Peer::Client, [](QcMessage &&) {});
        QcClientSession session(std::move(clientEnd), u"Probe"_s);
        session.start(QcClientSession::TickMode::Manual);

        server.send(QcMsg::Welcome{QcProtocol::version, 1, u"Fake"_s, 1, 0.0f, 0.0f, false});
        server.send(QcMsg::ContentSync{m_content});
        server.send(QcMsg::EntitySpawn{40, type, QString(), {0.5, 64.0, 0.5}, 0.0f, 0.0, {item, count, 0}, {}, 0.0f, 0, qcNoItem});
        QTRY_VERIFY_WITH_TIMEOUT(session.state() == QcClientSession::State::Disconnected || session.entities().contains(40), timeoutMs);
        QCOMPARE(session.entities().contains(40), accepted);
        if (!accepted) {
            QVERIFY(session.disconnectReason().contains(u"entity"_s));
            return;
        }
        QVERIFY(session.entities().value(40).isItem());

        // A new count for a known item keeps the entity and its motion history.
        server.send(QcMsg::EntityMoves{{{40, {0.5, 63.0, 0.5}, 0.0f, 0.0, false, 0.0f, 0, qcNoItem}}});
        server.send(QcMsg::EntitySpawn{40, type, QString(), {9.5, 64.0, 0.5}, 0.0f, 0.0, {item, 7, 0}, {}, 0.0f, 0, qcNoItem});
        QTRY_COMPARE_WITH_TIMEOUT(session.entities().value(40).item.count, 7, timeoutMs);
        // The newest snapshot without the interpolation delay (decision E-271): the move, not the spawn's place.
        session.entityMirror().setBuffered(false);
        session.entityMirror().tick();
        QCOMPARE(session.entities().value(40).position, (QcVec3{0.5, 63.0, 0.5}));
    }

    void garbageFrameClosesTheConnection()
    {
        QcServerConfig config;
        QcServer server(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        QcTransport *client = clientEnd.get();
        QSignalSpy closed(client, &QcTransport::closed);
        server.addClient(std::move(serverEnd));
        client->sendFrame(QByteArray("\xFF\xFF garbage", 10));
        QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, timeoutMs);
        server.tick();
        QCOMPARE(server.clientCount(), 0);
    }

    void serverRateLimitsInputs()
    {
        QcServerConfig config;
        config.viewDistance = 1;
        QcServer server(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        QcRawClient client(std::move(clientEnd));
        server.addClient(std::move(serverEnd));
        client.hello(u"Fast"_s);
        QTRY_VERIFY_WITH_TIMEOUT(client.entityId != 0, timeoutMs);

        // A cheating client floods 100 inputs within one tick; only the budget is simulated.
        for (quint32 i = 1; i <= 100; ++i) {
            QcPlayerInput input;
            input.sequence = i;
            input.forward = 1.0f;
            client.connection.send(QcMsg::PlayerInput{input});
        }
        QTest::qWait(50);
        server.tick();
        QTRY_VERIFY_WITH_TIMEOUT(client.acknowledged > 0, timeoutMs);
        QVERIFY2(client.acknowledged <= 11, qPrintable(QString::number(client.acknowledged)));
    }
};

QTEST_GUILESS_MAIN(TestClientServer)
#include "test_client_server.moc"
