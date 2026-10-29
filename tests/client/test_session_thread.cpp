// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/control/QcControlAim.h"
#include "clientcore/network/QcNetworkClient.h"
#include "clientcore/session/QcClientSession.h"
#include "common/physics/QcPhysics.h"
#include "fixtures/QcRecordingTransport.h"
#include "fixtures/QcTestTls.h"
#include "fixtures/QcTestUtil.h"
#include "server/core/QcServerConfig.h"
#include "server/network/QcNetworkServer.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worlds/QcLocalServerHost.h"

#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <cmath>
#include <memory>

using namespace Qt::StringLiterals;

// The client session with its tick core on a thread of its own (decision E-560), against a local
// server: what the session mirrors from the core stays right while the session's thread keeps sending
// commands (the wish to fly after a landing), the end of a session by the server keeps its reason,
// a dead player sends no inputs and comes back on the ground, riding works from the thread, and the
// same inputs give the same player as with the timer in the session's thread, without correction.
namespace {

constexpr int timeoutMs = 20'000;

// What a scripted walk sent and where it ended.
struct Walk
{
    QList<QcPlayerInput> inputs;
    QcVec3 position;
    double largestCorrection = 0.0;
};

} // namespace

class TestSessionThread : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();
    std::unique_ptr<QcLocalServerHost> m_host;
    std::unique_ptr<QcClientSession> m_session;
    // The session's transport; owned by the session.
    QcRecordingTransport *m_wire = nullptr;

    void startHost(QcGameMode mode)
    {
        QcServerConfig config;
        config.worldName = u"Thread"_s;
        config.seed = 7;
        config.viewDistance = 1;
        config.defaultGameMode = mode;
        m_host = std::make_unique<QcLocalServerHost>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_host->startError().isEmpty());
    }

    // A local session through a recording transport, playing and standing on the ground.
    void join(QcClientSession::TickMode mode = QcClientSession::TickMode::Threaded)
    {
        auto wire = std::make_unique<QcRecordingTransport>(m_host->connectClient());
        m_wire = wire.get();
        m_session = std::make_unique<QcClientSession>(std::move(wire), u"Player"_s);
        m_session->start(mode);
        QTRY_VERIFY_WITH_TIMEOUT(m_session->state() == QcClientSession::State::Playing, timeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(m_session->playerMotion().onGround, timeoutMs);
    }

    void waitTicks(int ticks)
    {
        const quint64 until = m_session->tickCount() + static_cast<quint64>(ticks);
        QTRY_VERIFY_WITH_TIMEOUT(m_session->tickCount() >= until, timeoutMs);
    }

    // Frames like the game's: each is busy for 3 ms and then sets the intent (a little different
    // each time, so that each is sent to the core), so a tick of the core usually ends during a frame
    // and its event is applied after the frame's intent, which the core has not seen yet.
    struct Frames
    {
        QTimer timer;
        QcPlayerInput intent;
        int frame = 0;
    };

    void startFrames(Frames &frames)
    {
        frames.timer.setInterval(4);
        connect(&frames.timer, &QTimer::timeout, this, [this, &frames] {
            QThread::msleep(3);
            QcPlayerInput intent = frames.intent;
            intent.yaw = static_cast<float>(++frames.frame % 2) * 0.01f;
            m_session->setIntent(intent);
        });
        frames.timer.start();
    }

    void takeOff(Frames &frames)
    {
        m_session->gameMode().toggleFlight();
        frames.intent = {};
        frames.intent.jump = true;
        const double ground = m_session->playerMotion().position.y;
        QTRY_VERIFY_WITH_TIMEOUT(m_session->playerMotion().flying && m_session->playerMotion().position.y > ground + 2.0, timeoutMs);
        frames.intent = {};
    }

    // The id of a minecart the session mirrors, 0 without one.
    quint32 minecartNearby() const
    {
        const QHash<quint32, QcClientEntity> &entities = m_session->entities();
        for (auto it = entities.cbegin(); it != entities.cend(); ++it) {
            if (it->isMinecart()) {
                return it.key();
            }
        }
        return 0;
    }

    qsizetype inputsSent() const { return m_wire->sentOf<QcMsg::PlayerInput>().size(); }

    // Plays the same script on the ticks of a new session ticking by `mode`.
    Walk walk(QcClientSession::TickMode mode)
    {
        startHost(QcGameMode::Survival);
        join(mode);
        waitTicks(10);
        m_session->resetCorrections();

        // Shared with the slot, which must not hold a reference to a local.
        const auto ticks = std::make_shared<int>(0);
        const QMetaObject::Connection script = connect(m_session.get(), &QcClientSession::ticked, this, [this, ticks] {
            const int tick = *ticks;
            QcPlayerInput intent;
            intent.forward = tick < 15 || (tick >= 26 && tick < 40) ? 1.0f : 0.0f;
            intent.strafe = tick >= 15 && tick < 25 ? 1.0f : 0.0f;
            intent.yaw = tick >= 15 ? 90.0f : 0.0f;
            intent.jump = tick == 25;
            intent.sprint = tick >= 26 && tick < 40;
            m_session->setIntent(intent);
            ++*ticks;
        });
        [[maybe_unused]] const bool done = QTest::qWaitFor([&ticks] { return *ticks >= 60; }, timeoutMs);
        disconnect(script);
        m_session->setIntent({});
        waitTicks(20);

        Walk result;
        result.position = m_session->playerMotion().position;
        result.largestCorrection = m_session->prediction().largestCorrection();
        bool moving = false;
        const QList<QcMsg::PlayerInput> sentInputs = m_wire->sentOf<QcMsg::PlayerInput>();
        for (QcMsg::PlayerInput sent : sentInputs) {
            sent.input.sequence = 0;
            moving = moving || sent.input != QcPlayerInput{};
            if (moving && result.inputs.size() < 45) {
                result.inputs.append(sent.input);
            }
        }
        cleanup();
        return result;
    }

private slots:
    void cleanup()
    {
        if (m_session) {
            m_session->disconnectFromServer();
            m_session.reset();
            m_wire = nullptr;
        }
        if (m_host) {
            QVERIFY(m_host->shutdown().has_value());
            m_host.reset();
        }
    }

    void flightWishAfterLanding()
    {
        // The frames keep sending intents; after a landing the wish to fly follows the core at once,
        // so a double tap right after starts a new flight instead of ending a stale one.
        startHost(QcGameMode::Creative);
        join();
        Frames frames;
        startFrames(frames);
        takeOff(frames);
        QVERIFY(m_session->gameMode().flightWanted());

        // From the tick of the landing on, no tick shows a landed player who still wants to fly.
        const auto staleTicks = std::make_shared<int>(0);
        const QMetaObject::Connection watching = connect(m_session.get(), &QcClientSession::ticked, this, [this, staleTicks] {
            const QcEntityMotion &motion = m_session->playerMotion();
            *staleTicks += motion.onGround && !motion.flying && m_session->gameMode().flightWanted() ? 1 : 0;
        });
        frames.intent.sneak = true;
        QTRY_VERIFY_WITH_TIMEOUT(m_session->playerMotion().onGround && !m_session->playerMotion().flying, timeoutMs);
        frames.intent = {};
        waitTicks(10);
        disconnect(watching);
        QCOMPARE(*staleTicks, 0);
        QVERIFY(!m_session->gameMode().flightWanted());

        m_session->gameMode().toggleFlight();
        QVERIFY(m_session->gameMode().flightWanted());
        frames.intent.jump = true;
        QTRY_VERIFY_WITH_TIMEOUT(m_session->playerMotion().flying, timeoutMs);
        QVERIFY(m_session->gameMode().flightWanted());
    }

    void serverStopsWhileTicking()
    {
        // The server's Disconnect comes before the close; its reason is the session's.
        startHost(QcGameMode::Survival);
        join();
        QSignalSpy disconnected(m_session.get(), &QcClientSession::disconnected);
        QVERIFY(m_host->shutdown().has_value());
        QTRY_COMPARE_WITH_TIMEOUT(m_session->state(), QcClientSession::State::Disconnected, timeoutMs);
        QCOMPARE(m_session->disconnectMessage().key, u"The server stopped"_s);
        QCOMPARE(m_session->disconnectReason(), u"The server stopped"_s);
        QCOMPARE(disconnected.size(), 1);
        QCOMPARE(disconnected.first().first().toString(), u"The server stopped"_s);
        // Nothing ticks any more.
        const quint64 ticks = m_session->tickCount();
        QTest::qWait(150);
        QCOMPARE(m_session->tickCount(), ticks);
    }

    void kickOverTcpWhileTicking()
    {
        // A network player is kicked while his session ticks on its thread: the reason survives the
        // close. A second one leaves and is deleted at once; the server sees both go.
        startHost(QcGameMode::Survival);
        QcNetworkConfig network;
        network.bindAddress = QHostAddress::LocalHost;
        network.port = 0;
        network.tls = QcTestTls::credentials();
        const QcResult<quint16> port = m_host->listen(network);
        QVERIFY(port.has_value());

        // Joined like the game joins: the TLS socket is made in the session's tick thread.
        const auto connectTcp = [this, &port](const QString &name) -> std::unique_ptr<QcClientSession> {
            QcNetworkClient client;
            QSignalSpy joined(&client, &QcNetworkClient::joined);
            QcNetworkClient::Request request;
            request.address = {u"127.0.0.1"_s, *port};
            request.playerName = name;
            request.connectTimeoutMs = timeoutMs;
            request.pinning = QcNetworkClient::Pinning::DisabledForTests;
            request.credentials.installationKey = QByteArray(32, name.at(0).toLatin1());
            request.credentials.viewDistance = 1;
            for (const QcPackReference &pack : m_content->packs()) {
                request.credentials.packs.append({pack.id, pack.version, {}});
            }
            client.join(request);
            return joined.wait(timeoutMs) ? client.takeSession() : nullptr;
        };
        std::unique_ptr<QcClientSession> guest = connectTcp(u"Guest"_s);
        std::unique_ptr<QcClientSession> leaver = connectTcp(u"Leaver"_s);
        QVERIFY(guest && leaver);
        QTRY_VERIFY_WITH_TIMEOUT(guest->state() == QcClientSession::State::Playing, timeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(leaver->state() == QcClientSession::State::Playing, timeoutMs);

        QSignalSpy disconnected(guest.get(), &QcClientSession::disconnected);
        QVERIFY(m_host->runConsoleCommand(u"kick Guest testing"_s).join(u' ').contains(u"Kicked Guest"_s));
        QTRY_COMPARE_WITH_TIMEOUT(guest->state(), QcClientSession::State::Disconnected, timeoutMs);
        QVERIFY2(guest->disconnectReason().contains(u"testing"_s), qPrintable(guest->disconnectReason()));
        QCOMPARE(disconnected.size(), 1);

        leaver->disconnectFromServer();
        leaver.reset();
        guest.reset();
        QTRY_VERIFY_WITH_TIMEOUT(m_host->runConsoleCommand(u"list"_s).join(u' ').contains(u"0 players online"_s), timeoutMs);
    }

    void deadPlayersSendNoInputs()
    {
        // Dying ends the flight and the inputs; after the respawn the player is on the ground and
        // sends again.
        startHost(QcGameMode::Creative);
        join();
        Frames frames;
        startFrames(frames);
        takeOff(frames);
        frames.intent.forward = 1.0f;

        m_session->sendChat(u"/kill"_s);
        QTRY_VERIFY_WITH_TIMEOUT(m_session->isDead(), timeoutMs);
        QVERIFY(!m_session->gameMode().flightWanted());
        waitTicks(3);
        const qsizetype whileDead = inputsSent();
        const QcVec3 lying = m_session->playerMotion().position;
        waitTicks(10);
        QCOMPARE(inputsSent(), whileDead);
        QCOMPARE(m_session->playerMotion().position, lying);
        QVERIFY(!m_session->gameMode().flightWanted());

        frames.intent = {};
        m_session->respawn();
        QTRY_VERIFY_WITH_TIMEOUT(!m_session->isDead(), timeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(inputsSent() > whileDead + 3, timeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(m_session->playerMotion().onGround, timeoutMs);
        QVERIFY(!m_session->gameMode().flightWanted());
        QVERIFY(!m_session->playerMotion().flying);
    }

    void ridingFromTheThread()
    {
        // Getting into a minecart, sitting in it and getting out again, with the core on its thread:
        // the player sits where the cart is and stands next to it afterwards without correction.
        startHost(QcGameMode::Survival);
        join();
        m_session->sendChat(u"/summon minecart"_s);
        QTRY_VERIFY_WITH_TIMEOUT(minecartNearby() != 0, timeoutMs);
        const quint32 cart = minecartNearby();

        const QcVec3 cartAt = m_session->entities().value(cart).position;
        QcControlAim::lookAtPoint(*m_session, cartAt + QcVec3{0.0, 0.3, 0.0});
        QTRY_COMPARE_WITH_TIMEOUT(m_session->hand().targetEntity(), std::optional<quint32>(cart), timeoutMs);
        QVERIFY(m_session->useItem());
        QTRY_COMPARE_WITH_TIMEOUT(m_session->prediction().riding().vehicle(), cart, timeoutMs);
        waitTicks(5);
        const QcVec3 seat = m_session->playerMotion().position;
        QVERIFY2((QcVec3{seat.x, 0.0, seat.z} - QcVec3{cartAt.x, 0.0, cartAt.z}).length() < 0.5, qPrintable(QString::number(seat.x)));

        QcPlayerInput sneak = m_session->intent();
        sneak.sneak = true;
        m_session->setIntent(sneak);
        QTRY_VERIFY_WITH_TIMEOUT(!m_session->prediction().riding().isRiding(), timeoutMs);
        m_session->setIntent({});
        QTRY_VERIFY_WITH_TIMEOUT(m_session->playerMotion().onGround, timeoutMs);
        waitTicks(5);
        m_session->resetCorrections();
        waitTicks(10);
        QVERIFY2(m_session->prediction().largestCorrection() < 1e-9, qPrintable(QString::number(m_session->prediction().largestCorrection())));
    }

    void sameWalkThreadedAndInline()
    {
        // The same inputs on the ticks give the same player either way, without correction. The
        // script reacts to `ticked`, so on a loaded machine a late session thread may give an input
        // to the next tick on the thread; then the walk is played again.
        const Walk inline_ = walk(QcClientSession::TickMode::Timer);
        QCOMPARE(inline_.inputs.size(), 45);
        QVERIFY2(inline_.largestCorrection < 1e-9, qPrintable(QString::number(inline_.largestCorrection)));

        Walk threaded;
        for (int attempt = 0; attempt < 3; ++attempt) {
            threaded = walk(QcClientSession::TickMode::Threaded);
            if (threaded.inputs == inline_.inputs) {
                break;
            }
            qInfo() << "The inputs of the threaded walk differ (late session thread); again";
        }
        QVERIFY(threaded.inputs == inline_.inputs);
        QVERIFY2(threaded.largestCorrection < 1e-9, qPrintable(QString::number(threaded.largestCorrection)));
        QVERIFY2((threaded.position - inline_.position).length() < 1e-9,
                 qPrintable(u"%1 %2 %3"_s.arg(threaded.position.x).arg(threaded.position.y).arg(threaded.position.z)));
    }
};

QTEST_GUILESS_MAIN(TestSessionThread)
#include "test_session_thread.moc"
