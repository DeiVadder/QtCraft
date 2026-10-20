#include "clientcore/network/QcNetworkClient.h"
#include "clientcore/session/QcClientSession.h"
#include "QcTestUtil.h"
#include "server/core/QcServerConfig.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worlds/QcLocalServerHost.h"

#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 20'000;

} // namespace

// Joining a network game from the client (decision E-251, the API of the join menu): connect, log
// in, take over the session; refusals and unreachable servers come back as failures.
class TestNetworkClient : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    std::unique_ptr<QcLocalServerHost> host(QcServerConfig config = {})
    {
        config.viewDistance = 2;
        auto server = std::make_unique<QcLocalServerHost>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QcNetworkConfig network;
        network.bindAddress = QHostAddress::LocalHost;
        network.port = 0;
        if (!server->listen(network)) {
            return nullptr;
        }
        return server;
    }

    QcNetworkClient::Request request(quint16 port, const QString &name, const QString &password = {}) const
    {
        QcNetworkClient::Request request;
        request.address = {u"127.0.0.1"_s, port};
        request.playerName = name;
        request.credentials.installationKey = QByteArray(32, name.isEmpty() ? 'x' : name.at(0).toLatin1());
        request.credentials.password = password;
        request.credentials.viewDistance = 1;
        for (const QcPackReference &pack : m_content->packs()) {
            request.credentials.packs.append({pack.id, pack.version, {}});
        }
        return request;
    }

private slots:
    void joinsAndHandsOverAPlayingSession()
    {
        QcServerConfig config;
        config.serverName = u"Kiste"_s;
        config.motd = u"Hallo"_s;
        std::unique_ptr<QcLocalServerHost> server = host(config);
        QVERIFY(server);

        QcNetworkClient client;
        QSignalSpy joined(&client, &QcNetworkClient::joined);
        QSignalSpy failed(&client, &QcNetworkClient::failed);
        QSignalSpy stages(&client, &QcNetworkClient::stageChanged);
        client.join(request(server->listeningPort(), u"Ann"_s));
        QCOMPARE(client.stage(), QcNetworkClient::Stage::Connecting);
        QVERIFY(joined.wait(timeoutMs));
        QCOMPARE(failed.count(), 0);
        QCOMPARE(stages.at(1).first().value<QcNetworkClient::Stage>(), QcNetworkClient::Stage::LoggingIn);

        const std::unique_ptr<QcClientSession> session = client.takeSession();
        QVERIFY(session);
        QVERIFY(!client.takeSession());
        QCOMPARE(session->login().serverName(), u"Kiste"_s);
        QCOMPARE(session->login().motd(), u"Hallo"_s);
        QCOMPARE(session->welcome().viewDistance, 1);
        QTRY_COMPARE_WITH_TIMEOUT(session->state(), QcClientSession::State::Playing, timeoutMs);
        QTRY_VERIFY_WITH_TIMEOUT(!session->players().isEmpty() && session->players().first().name == u"Ann"_s, timeoutMs);
        QVERIFY(session->world() && session->content());

        session->disconnectFromServer();
        QVERIFY(server->shutdown().has_value());
    }

    void aRefusalComesBackWithItsKey()
    {
        QcServerConfig config;
        config.password = u"secret"_s;
        std::unique_ptr<QcLocalServerHost> server = host(config);
        QVERIFY(server);

        QcNetworkClient client;
        QSignalSpy joined(&client, &QcNetworkClient::joined);
        QSignalSpy failed(&client, &QcNetworkClient::failed);
        client.join(request(server->listeningPort(), u"Bob"_s, u"wrong"_s));
        QVERIFY(failed.wait(timeoutMs));
        QCOMPARE(joined.count(), 0);
        QCOMPARE(failed.first().at(0).toString(), u"Wrong server password"_s);
        QCOMPARE(failed.first().at(1).value<QcMsg::Disconnect>().key, u"Wrong server password"_s);
        QVERIFY(!client.takeSession());

        // With the right password the same client gets in.
        client.join(request(server->listeningPort(), u"Bob"_s, u"secret"_s));
        QVERIFY(joined.wait(timeoutMs));
        QVERIFY(client.takeSession());
        QVERIFY(server->shutdown().has_value());
    }

    void anUnreachableServerFails()
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        const quint16 port = probe.serverPort();
        probe.close();

        QcNetworkClient client;
        QSignalSpy failed(&client, &QcNetworkClient::failed);
        client.join(request(port, u"Cid"_s));
        QVERIFY(failed.wait(timeoutMs));
        QVERIFY(!failed.first().at(0).toString().isEmpty());
        QVERIFY(failed.first().at(1).value<QcMsg::Disconnect>().key.isEmpty());
        QCOMPARE(client.stage(), QcNetworkClient::Stage::Idle);
    }

    void cancellingEndsQuietly()
    {
        std::unique_ptr<QcLocalServerHost> server = host();
        QVERIFY(server);
        QcNetworkClient client;
        QSignalSpy joined(&client, &QcNetworkClient::joined);
        QSignalSpy failed(&client, &QcNetworkClient::failed);
        client.join(request(server->listeningPort(), u"Dee"_s));
        client.cancel();
        QTest::qWait(300);
        QCOMPARE(joined.count() + failed.count(), 0);
        QCOMPARE(client.stage(), QcNetworkClient::Stage::Idle);
        QVERIFY(server->shutdown().has_value());
    }

    void cancellingWhileLoggingInClosesTheSession()
    {
        // A server that accepts and never answers the Hello: the join waits in the login stage.
        QTcpServer silent;
        QVERIFY(silent.listen(QHostAddress::LocalHost, 0));
        QcNetworkClient client;
        QSignalSpy joined(&client, &QcNetworkClient::joined);
        QSignalSpy failed(&client, &QcNetworkClient::failed);
        client.join(request(silent.serverPort(), u"Eve"_s));
        QTRY_COMPARE_WITH_TIMEOUT(client.stage(), QcNetworkClient::Stage::LoggingIn, timeoutMs);
        QVERIFY(silent.hasPendingConnections() || silent.waitForNewConnection(timeoutMs));
        QTcpSocket *peer = silent.nextPendingConnection();
        QVERIFY(peer);
        // The Hello arrived.
        QTRY_VERIFY_WITH_TIMEOUT(peer->bytesAvailable() > 0, timeoutMs);

        client.cancel();
        QCOMPARE(client.stage(), QcNetworkClient::Stage::Idle);
        QVERIFY(!client.takeSession());
        // The session goes (deleteLater) and with it the connection; nothing is reported.
        QTRY_COMPARE_WITH_TIMEOUT(peer->state(), QAbstractSocket::UnconnectedState, timeoutMs);
        QTest::qWait(100);
        QCOMPARE(joined.count() + failed.count(), 0);

        // The client can join again afterwards.
        std::unique_ptr<QcLocalServerHost> server = host();
        QVERIFY(server);
        client.join(request(server->listeningPort(), u"Eve"_s));
        QVERIFY(joined.wait(timeoutMs));
        QVERIFY(client.takeSession());
        QVERIFY(server->shutdown().has_value());
    }
};

QTEST_MAIN(TestNetworkClient)
#include "test_network_client.moc"
