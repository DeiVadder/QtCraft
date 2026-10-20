#include "clientcore/session/QcClientSession.h"
#include "common/physics/QcPhysics.h"
#include "common/protocol/QcConnection.h"
#include "common/protocol/QcInProcessTransport.h"
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

    void send(const QcMessage &message) { m_connection.send(message); }

    QList<QcPlayerInput> inputs;
    // The look sent with Hello and every later SetAppearance.
    QcAppearance helloAppearance;
    QList<QcAppearance> appearances;

private:
    void onMessage(QcMessage &&message)
    {
        if (const auto *hello = std::get_if<QcMsg::Hello>(&message)) {
            helloAppearance = hello->appearance;
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
        } else if (const auto *look = std::get_if<QcMsg::SetAppearance>(&message)) {
            appearances.append(look->appearance);
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
        QCOMPARE(session.prediction().pendingInputCount(), 3);

        // The server disagrees: after input 1 the player stands at x = 5.5, already at walking speed.
        QcEntityMotion corrected;
        corrected.position = {5.5, 64.0, 0.5};
        corrected.velocity = {QcPhysics::walkSpeed, 0.0, 0.0};
        corrected.onGround = true;
        server.sendState(1, corrected);
        QTRY_COMPARE_WITH_TIMEOUT(session.prediction().pendingInputCount(), 2, timeoutMs);

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
        QTRY_COMPARE_WITH_TIMEOUT(session.prediction().pendingInputCount(), 0, timeoutMs);
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
        QTRY_COMPARE_WITH_TIMEOUT(session.prediction().pendingInputCount(), 2, timeoutMs);
        const QcEntityMotion before = session.playerMotion();

        // An older acknowledgement arriving late must neither roll back the prediction nor drop inputs.
        QcEntityMotion stale = still;
        stale.position.x = -20.0;
        server.sendState(1, stale);
        QTest::qWait(100);
        QCOMPARE(session.prediction().pendingInputCount(), 2);
        QCOMPARE(session.playerMotion(), before);
    }

    void remoteEntitiesAnimateFromServerState()
    {
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        FakeServer server(std::move(serverEnd));
        QcClientSession session(std::move(clientEnd), u"Tester"_s);
        session.start(QcClientSession::TickMode::Manual);
        QTRY_VERIFY_WITH_TIMEOUT(session.state() == QcClientSession::State::Playing, timeoutMs);

        constexpr quint32 bob = 7;
        QcMsg::EntitySpawn spawn;
        spawn.id = bob;
        spawn.position = {0.5, 64.0, 3.5};
        spawn.health = 20.0;
        spawn.flags = QcMsg::OnGround;
        server.send(spawn);
        QTRY_VERIFY_WITH_TIMEOUT(session.entities().contains(bob), timeoutMs);
        const auto pose = [&] { return QcLocalAnimation::poseOf(session.entities().value(bob), 0.0); };
        QCOMPARE(pose().swingProgress, -1.0);
        QCOMPARE(pose().hurtProgress, -1.0);

        // Animation from the server state, each tick showing the newest snapshot; the interpolation
        // buffer has its own test (test_interpolation, decision E-271).
        session.entityMirror().setBuffered(false);
        qint64 serverMs = 0;
        const auto arrive = [&](QList<QcMsg::EntityMove> moves) {
            serverMs += 50;
            server.send(QcMsg::EntityMoves{std::move(moves), serverMs});
            QTRY_COMPARE_WITH_TIMEOUT(session.entityMirror().clock().latestServerMs(), serverMs, timeoutMs);
        };

        // Walking at 4 m/s while the moves arrive unevenly: two in one client tick, none in the next.
        double x = 0.5;
        const auto step = [&](quint8 flags, bool hurt) {
            x += 0.2;
            return QcMsg::EntityMove{bob, {x, 64.0, 3.5}, 0.0f, 20.0, hurt, 0.0f, flags, qcNoItem};
        };
        for (int i = 0; i < 20; ++i) {
            if (i % 2 == 0) {
                arrive({step(QcMsg::OnGround, false), step(QcMsg::OnGround, false)});
            }
            session.tick();
            QCOMPARE(session.entities().value(bob).position.x, x);
            if (i >= 10) {
                QVERIFY2(pose().speed > 2.0 && pose().speed < 6.0, qPrintable(QString::number(pose().speed)));
            }
        }

        // A swing flag starts the arm swing, which ends after its length; a hurt move flashes.
        arrive({step(QcMsg::OnGround | QcMsg::Swing, true)});
        QCOMPARE(pose().swingProgress, 0.0);
        QVERIFY(pose().hurtProgress >= 0.0);
        for (int i = 0; i < QcLocalAnimation::swingTicks; ++i) {
            session.tick();
        }
        QCOMPARE(pose().swingProgress, -1.0);
        QCOMPARE(pose().hurtProgress, -1.0);

        // Dying: the entity leaves the live list and plays the death animation for its length,
        // even when the player respawns meanwhile.
        server.send(QcMsg::EntityRemove{bob, true});
        QTRY_VERIFY_WITH_TIMEOUT(session.entityMirror().dying().contains(bob), timeoutMs);
        QVERIFY(!session.entities().contains(bob));
        QCOMPARE(QcLocalAnimation::poseOf(session.entityMirror().dying().value(bob), 0.0).deathProgress, 0.0);
        session.tick();
        QVERIFY(QcLocalAnimation::poseOf(session.entityMirror().dying().value(bob), 0.0).deathProgress > 0.0);

        spawn.position = {5.5, 64.0, 5.5};
        server.send(spawn);
        QTRY_VERIFY_WITH_TIMEOUT(session.entities().contains(bob), timeoutMs);
        QVERIFY(session.entityMirror().dying().contains(bob));
        for (int i = 1; i < QcLocalAnimation::deathTicks; ++i) {
            session.tick();
        }
        QVERIFY(!session.entityMirror().dying().contains(bob));
        QCOMPARE(session.entities().value(bob).position.x, 5.5);
        QCOMPARE(pose().deathProgress, -1.0);
    }

    void lookChangedWhileConnectingReachesTheServer()
    {
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        FakeServer server(std::move(serverEnd));
        QcClientSession session(std::move(clientEnd), u"Tester"_s);
        session.start(QcClientSession::TickMode::Manual);
        QCOMPARE(session.state(), QcClientSession::State::Connecting);
        QcAppearance look;
        look.hair = u"t:bun"_s;
        session.setAppearance(look);
        QTRY_COMPARE_WITH_TIMEOUT(server.appearances.size(), 1, timeoutMs);
        QCOMPARE(server.helloAppearance.hair, QcAppearance().hair);
        QCOMPARE(server.appearances.first().hair, u"t:bun"_s);
    }

    void aLateMachineAnswerAfterClosingIsIgnored()
    {
        auto [serverEnd, clientEnd] = QcInProcessTransport::createPair();
        FakeServer server(std::move(serverEnd));
        QcClientSession session(std::move(clientEnd), u"Tester"_s);
        session.start(QcClientSession::TickMode::Manual);
        QTRY_VERIFY_WITH_TIMEOUT(session.state() == QcClientSession::State::Playing, timeoutMs);
        server.send(QcMsg::OpenMachine{{2, 64, 0}, u"smelting"_s});
        QTRY_VERIFY_WITH_TIMEOUT(session.machine().isOpen(), timeoutMs);
        session.closeMachine();
        QVERIFY(!session.machine().isOpen());

        // The answer to a second click, sent before the server saw the close.
        server.send(QcMsg::OpenMachine{{2, 64, 0}, u"smelting"_s});
        QTest::qWait(100);
        QVERIFY(!session.machine().isOpen());
    }
};

QTEST_GUILESS_MAIN(TestReconciliation)
#include "test_reconciliation.moc"
