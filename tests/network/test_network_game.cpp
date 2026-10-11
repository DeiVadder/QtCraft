#include "QcConnectionLimiter.h"
#include "QcLocalServerHost.h"
#include "QcNetworkServer.h"
#include "QcServerTestFixture.h"
#include "QcTcpConnector.h"
#include "QcTestUtil.h"
#include "QcWorldStorage.h"

#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 10'000;

} // namespace

// Several clients over TCP on the loopback interface (decisions E-251 to E-254): they see each
// other, chat and see each other's block changes; lost connections remove and save the player;
// connection floods are refused. Server, listener and clients share the test's thread, the ticks
// come from the test; one case runs the server in its own thread like the game does.
class TestNetworkGame : public QcServerTestFixture
{
    Q_OBJECT

private:
    std::unique_ptr<QcNetworkServer> m_network;
    quint16 m_port = 0;

    void startServer(QcServerConfig config, QcNetworkConfig network = {})
    {
        config.viewDistance = std::max(config.viewDistance, 1);
        if (config.operators.isEmpty()) {
            config.operators = {u"Ann"_s};
        }
        m_server = std::make_unique<QcServer>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(m_server->openWorld().has_value());
        network.bindAddress = QHostAddress::LocalHost;
        network.port = 0;
        m_network = std::make_unique<QcNetworkServer>(*m_server, network);
        const QcResult<quint16> port = m_network->listen();
        QVERIFY2(port.has_value(), qcErrorText(port).constData());
        m_port = *port;
    }

    // A raw client over TCP; null when the connection could not be opened.
    std::unique_ptr<RawClient> connectRaw(const QcTcpTransport::Limits &limits = {})
    {
        QcTcpConnector connector;
        QSignalSpy connected(&connector, &QcTcpConnector::connected);
        connector.connectTo({u"127.0.0.1"_s, m_port}, timeoutMs, limits);
        if (!connected.wait(timeoutMs)) {
            return nullptr;
        }
        return std::make_unique<RawClient>(connector.takeTransport());
    }

    std::unique_ptr<RawClient> join(const QString &name, const QcTcpTransport::Limits &limits = {})
    {
        std::unique_ptr<RawClient> client = connectRaw(limits);
        if (!client) {
            return nullptr;
        }
        client->hello(name);
        for (int i = 0; i < 100 && client->entityId == 0 && client->disconnectReason.isEmpty(); ++i) {
            QTest::qWait(5);
            ticks(1);
        }
        return client;
    }

    // Ticks until `done` holds or the time is up.
    template<typename Done>
    bool tickUntil(Done done, int maxTicks = 400)
    {
        for (int i = 0; i < maxTicks; ++i) {
            if (done()) {
                return true;
            }
            QTest::qWait(5);
            ticks(1);
        }
        return done();
    }

private slots:
    void cleanup()
    {
        m_client.reset();
        m_network.reset();
        m_server.reset();
    }

    void playersSeeEachOtherChatAndBuildTogether()
    {
        startServer({});
        std::vector<std::unique_ptr<RawClient>> players;
        for (const QString &name : {u"Ann"_s, u"Bob"_s, u"Cid"_s, u"Dee"_s}) {
            players.push_back(join(name));
            QVERIFY(players.back() && players.back()->entityId != 0);
        }
        QCOMPARE(m_server->clientCount(), 4);

        // Everyone sees the other three as players and lists all four.
        QVERIFY(tickUntil([&] {
            return std::ranges::all_of(players, [&](const std::unique_ptr<RawClient> &viewer) {
                return viewer->playerList.size() == 4 && std::ranges::all_of(players, [&](const std::unique_ptr<RawClient> &other) {
                    return other == viewer || viewer->entities.value(other->entityId, 0) == QcMsg::EntitySpawn::playerType;
                });
            });
        }));

        // A chat line reaches everyone.
        players[1]->connection.send(QcMsg::ChatInput{u"hello from Bob"_s});
        QVERIFY(tickUntil([&] {
            return std::ranges::all_of(players, [](const std::unique_ptr<RawClient> &player) {
                return std::ranges::any_of(player->chatLines, [](const QString &line) { return line.contains(u"hello from Bob"_s); });
            });
        }));

        // A block set by the operator reaches everyone.
        const QcVec3 at = players[0]->motion.position;
        const QcBlockPos pos{int(std::floor(at.x)) + 2, int(std::floor(at.y)), int(std::floor(at.z))};
        players[0]->connection.send(QcMsg::ChatInput{u"/setblock %1 %2 %3 glass"_s.arg(pos.x).arg(pos.y).arg(pos.z)});
        const QcBlockId glass = *m_content->blockId(u"qtcraft:glass"_s);
        QVERIFY(tickUntil([&] {
            return std::ranges::all_of(players, [&](const std::unique_ptr<RawClient> &player) {
                return std::ranges::any_of(player->blockUpdates, [&](QcMsg::BlockUpdate update) { return update.pos == pos && update.block == glass; });
            });
        }));
    }

    void aLostConnectionRemovesAndSavesThePlayer()
    {
        QTemporaryDir directory;
        QcServerConfig config;
        config.worldDirectory = directory.path();
        QcNetworkConfig network;
        // Long enough for slow logins under ThreadSanitizer; Ann keeps her connection alive meanwhile.
        network.transport.idleTimeoutMs = 2'000;
        network.transport.keepAliveMs = 50;
        startServer(config, network);

        // One leaves by closing the socket, one goes silent without closing. The first keeps its
        // link alive meanwhile, however long the second login takes (slow under sanitizers).
        std::unique_ptr<RawClient> closing = join(u"Ann"_s, {.keepAliveMs = 50});
        QVERIFY(closing && closing->entityId != 0);
        // Without keep-alives of its own.
        std::unique_ptr<RawClient> silent = join(u"Bob"_s, {.keepAliveMs = 3'600'000});
        QVERIFY(silent && silent->entityId != 0);
        QCOMPARE(m_server->clientCount(), 2);

        // Ann keeps her link alive, so only the closed socket removes and saves her. (Bob may time
        // out meanwhile when the run is slow.)
        closing.reset();
        const QString annFile = QDir(directory.path()).filePath(u"players/"_s + QcWorldStorage::playerFileName(u"Ann"_s));
        QVERIFY(tickUntil([&] { return QFile::exists(annFile); }));
        QVERIFY(m_server->clientCount() <= 1);

        // The silent client sends nothing after its login: the server notices within its timeout.
        QVERIFY(tickUntil([&] { return m_server->clientCount() == 0; }, 2'000));
        QVERIFY(silent->connection.isOpen() || !silent->connection.lastError().isEmpty());
        QVERIFY(QFile::exists(QDir(directory.path()).filePath(u"players/"_s + QcWorldStorage::playerFileName(u"Bob"_s))));
    }

    void connectionFloodsAreRefused()
    {
        QcNetworkConfig network;
        network.connections = {.perSecond = 0.001, .burst = 3.0, .perAddress = 8, .total = 64};
        startServer({}, network);

        std::vector<std::unique_ptr<QTcpSocket>> sockets;
        for (int i = 0; i < 6; ++i) {
            auto socket = std::make_unique<QTcpSocket>();
            socket->connectToHost(QHostAddress::LocalHost, m_port);
            QVERIFY(socket->waitForConnected(timeoutMs));
            sockets.push_back(std::move(socket));
        }
        // The listener accepts and at once closes everything beyond the burst.
        QTRY_COMPARE_WITH_TIMEOUT(m_network->refusedConnections(), 3u, timeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(std::ranges::count_if(sockets, [](const auto &s) { return s->state() == QAbstractSocket::UnconnectedState; }) == 3,
                                 timeoutMs);
        QCOMPARE(m_server->clientCount(), 3);
        QCOMPARE(m_network->openConnections(), 3);

        // Closed connections count no more.
        sockets.clear();
        QVERIFY(tickUntil([&] { return m_network->openConnections() == 0; }));
    }

    void anAddressMayOpenOnlySoManyConnections()
    {
        QcNetworkConfig network;
        network.connections = {.perSecond = 100.0, .burst = 100.0, .perAddress = 2, .total = 64};
        startServer({}, network);
        std::unique_ptr<RawClient> first = join(u"Ann"_s);
        std::unique_ptr<RawClient> second = join(u"Bob"_s);
        QVERIFY(first && second && first->entityId != 0 && second->entityId != 0);
        std::unique_ptr<RawClient> third = connectRaw();
        QVERIFY(third);
        QVERIFY(tickUntil([&] { return !third->connection.isOpen(); }));
        QCOMPARE(m_network->refusedConnections(), 1u);

        // When one leaves, there is room again.
        first.reset();
        QVERIFY(tickUntil([&] { return m_network->openConnections() == 1; }));
        QVERIFY(join(u"Cid"_s)->entityId != 0);
    }

    void limitsCountIpv6PerNetwork()
    {
        // IPv6 hosts pick the lower 64 bits freely: the limits count the /64 network.
        QCOMPARE(QcConnectionLimiter::sourceOf(QHostAddress(u"2001:db8:1:2:aaaa::1"_s)), u"2001:db8:1:2::/64"_s);
        QCOMPARE(QcConnectionLimiter::sourceOf(QHostAddress(u"2001:db8:1:2:ffff:1:2:3"_s)), u"2001:db8:1:2::/64"_s);
        QVERIFY(QcConnectionLimiter::sourceOf(QHostAddress(u"2001:db8:1:3::1"_s)) != u"2001:db8:1:2::/64"_s);
        // IPv4, also as seen through an IPv6 socket, counts per address.
        QCOMPARE(QcConnectionLimiter::sourceOf(QHostAddress(u"192.0.2.7"_s)), u"192.0.2.7"_s);
        QCOMPARE(QcConnectionLimiter::sourceOf(QHostAddress(u"::ffff:192.0.2.7"_s)), u"192.0.2.7"_s);
        QCOMPARE(QcConnectionLimiter::sourceOf(QHostAddress(QHostAddress::LocalHostIPv6)), u"::/64"_s);

        QcConnectionLimiter limiter({.perSecond = 1.0, .burst = 2.0, .perAddress = 8, .total = 64});
        QVERIFY(limiter.admit(QcConnectionLimiter::sourceOf(QHostAddress(u"2001:db8::1"_s)), 0));
        QVERIFY(limiter.admit(QcConnectionLimiter::sourceOf(QHostAddress(u"2001:db8::2"_s)), 0));
        QVERIFY(!limiter.admit(QcConnectionLimiter::sourceOf(QHostAddress(u"2001:db8::3"_s)), 0));
        QVERIFY(limiter.admit(QcConnectionLimiter::sourceOf(QHostAddress(u"2001:db8:0:1::1"_s)), 0));
    }

    void unauthenticatedClientsHaveTinyFrames()
    {
        startServer({});
        std::unique_ptr<RawClient> client = connectRaw();
        QVERIFY(client);
        // A frame beyond the server's incoming limit ends the connection before any decoding.
        client->connection.send(QcMsg::ChatInput{QString(200'000, u'x')});
        QVERIFY(tickUntil([&] { return !client->connection.isOpen(); }));
        QCOMPARE(m_server->clientCount(), 0);
    }

    void theHostListensBesideItsLocalPlayer()
    {
        // The game's way: the server in its own thread, the local player in-process, a remote one over TCP.
        QcServerConfig config;
        config.viewDistance = 1;
        QcLocalServerHost host(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(host.startError().isEmpty());
        QcRawClient local(host.connectClient());
        local.hello(u"Host"_s);
        QTRY_VERIFY_WITH_TIMEOUT(local.entityId != 0, timeoutMs);

        QcNetworkConfig network;
        network.bindAddress = QHostAddress::LocalHost;
        network.port = 0;
        const QcResult<quint16> port = host.listen(network);
        QVERIFY2(port.has_value(), qcErrorText(port).constData());
        QCOMPARE(host.listeningPort(), *port);
        m_port = *port;

        std::unique_ptr<RawClient> remote = connectRaw();
        QVERIFY(remote);
        remote->hello(u"Guest"_s);
        QTRY_VERIFY_WITH_TIMEOUT(remote->entityId != 0, timeoutMs);
        QTRY_COMPARE_WITH_TIMEOUT(local.entities.value(remote->entityId, 0), QcMsg::EntitySpawn::playerType, timeoutMs);
        QTRY_COMPARE_WITH_TIMEOUT(remote->entities.value(local.entityId, 0), QcMsg::EntitySpawn::playerType, timeoutMs);
        QVERIFY(host.runConsoleCommand(u"list"_s).join(u' ').contains(u"2 players online"_s));

        // Stopping to host tells the remote player why and saves him; the host plays on.
        host.stopListening();
        QCOMPARE(host.listeningPort(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(remote->disconnectKey, u"The host stopped sharing the world"_s, timeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(host.runConsoleCommand(u"list"_s).join(u' ').contains(u"1 players online: Host"_s), timeoutMs);
        QVERIFY(local.disconnectKey.isEmpty());
        QVERIFY(host.shutdown().has_value());
        QTRY_COMPARE_WITH_TIMEOUT(local.disconnectKey, u"The server stopped"_s, timeoutMs);
    }
};

QTEST_GUILESS_MAIN(TestNetworkGame)
#include "test_network_game.moc"
