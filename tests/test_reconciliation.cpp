#include "QcClientSession.h"
#include "QcConnection.h"
#include "QcInProcessTransport.h"
#include "QcPhysics.h"
#include "QcTestWorld.h"

#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 10000;
constexpr QcBlockId stone = 1;

// Scripted server: answers the handshake with a flat stone world and records player inputs.
class FakeServer
{
public:
    explicit FakeServer(std::unique_ptr<QcTransport> transport)
        : m_content(std::make_shared<QcContentRegistry>(qcMakeTestContent()))
        , m_connection(std::move(transport), QcProtocol::Peer::Client, [this](QcMessage &&message) { onMessage(std::move(message)); })
    {
    }

    void sendState(quint32 acknowledged, const QcEntityMotion &motion)
    {
        m_connection.send(QcMsg::PlayerState{acknowledged, motion});
    }

    QList<QcPlayerInput> inputs;

private:
    void onMessage(QcMessage &&message)
    {
        if (std::holds_alternative<QcMsg::Hello>(message)) {
            m_connection.send(QcMsg::Welcome{QcProtocol::version, 1, u"Fake"_s, 0});
            m_connection.send(QcMsg::ContentSync{m_content});
            for (int cx = -1; cx <= 1; ++cx) {
                for (int cz = -1; cz <= 1; ++cz) {
                    auto chunk = std::make_shared<QcChunk>(QcChunkPos{cx, cz});
                    for (int x = 0; x < 16; ++x) {
                        for (int z = 0; z < 16; ++z) {
                            chunk->setBlock(x, 63, z, stone);
                        }
                    }
                    m_connection.send(QcMsg::ChunkData{chunk});
                }
            }
            QcEntityMotion spawn;
            spawn.position = {0.5, 64.0, 0.5};
            spawn.onGround = true;
            sendState(0, spawn);
        } else if (const auto *input = std::get_if<QcMsg::PlayerInput>(&message)) {
            inputs.append(input->input);
        }
    }

    std::shared_ptr<const QcContentRegistry> m_content;
    QcConnection m_connection;
};

} // namespace

class TestReconciliation : public QObject
{
    Q_OBJECT

private slots:
    void replaysUnacknowledgedInputsOnTopOfServerState()
    {
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        FakeServer server(std::move(serverEnd));
        QcClientSession session(std::move(clientEnd), u"Tester"_s);
        session.start(QcClientSession::TickMode::Manual);
        QTRY_VERIFY_WITH_TIMEOUT(session.state() == QcClientSession::State::Playing, timeoutMs);
        QTRY_COMPARE_WITH_TIMEOUT(session.world()->chunkCount(), 9, timeoutMs);

        QcPlayerInput east;
        east.forward = 1.0f;
        east.yaw = 90.0f;
        session.setIntent(east);
        for (int i = 0; i < 3; ++i) {
            session.tick();
        }
        QTRY_COMPARE_WITH_TIMEOUT(server.inputs.size(), 3, timeoutMs);
        QCOMPARE(server.inputs.constLast().sequence, 3u);
        QCOMPARE(session.pendingInputCount(), 3);

        // The server disagrees: after input 1 the player stands at x = 5.5, already at walking speed.
        QcEntityMotion corrected;
        corrected.position = {5.5, 64.0, 0.5};
        corrected.velocity = {QcPhysics::walkSpeed, 0.0, 0.0};
        corrected.onGround = true;
        server.sendState(1, corrected);
        QTRY_COMPARE_WITH_TIMEOUT(session.pendingInputCount(), 2, timeoutMs);

        // Inputs 2 and 3 are replayed: at full walking speed each tick moves 4.3 m/s * 0.05 s = 0.215 m east.
        const QcEntityMotion &motion = session.playerMotion();
        QVERIFY2(std::abs(motion.position.x - 5.93) < 1e-9, qPrintable(QString::number(motion.position.x, 'f', 6)));
        QCOMPARE(motion.position.y, 64.0);
        QCOMPARE(motion.position.z, 0.5);
        QVERIFY(motion.onGround);

        // Once everything is acknowledged the client takes the server state as it is.
        QcEntityMotion final = corrected;
        final.position.x = 7.0;
        final.velocity = {};
        server.sendState(3, final);
        QTRY_COMPARE_WITH_TIMEOUT(session.pendingInputCount(), 0, timeoutMs);
        QCOMPARE(session.playerMotion(), final);
    }

    void staleStateDoesNotDropNewerInputs()
    {
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        FakeServer server(std::move(serverEnd));
        QcClientSession session(std::move(clientEnd), u"Tester"_s);
        session.start(QcClientSession::TickMode::Manual);
        QTRY_VERIFY_WITH_TIMEOUT(session.state() == QcClientSession::State::Playing, timeoutMs);
        for (int i = 0; i < 4; ++i) {
            session.tick();
        }
        QcEntityMotion still;
        still.position = {0.5, 64.0, 0.5};
        still.onGround = true;
        server.sendState(2, still);
        QTRY_COMPARE_WITH_TIMEOUT(session.pendingInputCount(), 2, timeoutMs);
        const QcEntityMotion before = session.playerMotion();
        // An older acknowledgement arriving late must neither roll back the prediction nor drop inputs.
        QcEntityMotion stale = still;
        stale.position.x = -20.0;
        server.sendState(1, stale);
        QTest::qWait(100);
        QCOMPARE(session.pendingInputCount(), 2);
        QCOMPARE(session.playerMotion(), before);
    }
};

QTEST_GUILESS_MAIN(TestReconciliation)
#include "test_reconciliation.moc"
