#include "QcBaseContent.h"
#include "QcClientSession.h"
#include "QcConnection.h"
#include "QcInProcessTransport.h"
#include "QcLocalServerHost.h"
#include "QcPhysics.h"
#include "QcProtocol.h"
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
        host->shutdown();
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
        host->shutdown();
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
        host->shutdown();
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
        connection.send(QcMsg::Hello{QcProtocol::version + 1, u"Old"_s});
        QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, timeoutMs);
        QCOMPARE(reasons.size(), 1);
        QVERIFY(reasons.constFirst().contains(u"version"_s));
        host->shutdown();
    }

    void messagesBeforeHelloAreRejected()
    {
        auto host = makeHost(m_content);
        QcConnection connection(host->connectClient(), QcProtocol::Peer::Server, [](QcMessage &&) {});
        QSignalSpy closed(&connection, &QcConnection::closed);
        connection.send(QcMsg::PlayerInput{});
        QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, timeoutMs);
        host->shutdown();
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
        QList<QcMsg::PlayerState> states;
        QcConnection client(std::move(clientEnd), QcProtocol::Peer::Server, [&states](QcMessage &&message) {
            if (const auto *state = std::get_if<QcMsg::PlayerState>(&message)) {
                states.append(*state);
            }
        });
        server.addClient(std::move(serverEnd));
        client.send(QcMsg::Hello{QcProtocol::version, u"Fast"_s});
        QTest::qWait(50);
        // A cheating client floods 100 inputs within one tick; only the budget is simulated.
        for (quint32 i = 1; i <= 100; ++i) {
            QcPlayerInput input;
            input.sequence = i;
            input.forward = 1.0f;
            client.send(QcMsg::PlayerInput{input});
        }
        QTest::qWait(50);
        server.tick();
        QTRY_VERIFY_WITH_TIMEOUT(!states.isEmpty() && states.constLast().lastInputSequence > 0, timeoutMs);
        QVERIFY2(states.constLast().lastInputSequence <= 11, qPrintable(QString::number(states.constLast().lastInputSequence)));
    }
};

QTEST_GUILESS_MAIN(TestClientServer)
#include "test_client_server.moc"
