#include "network/QcTcpTransport.h"
#include "QcRawClient.h"
#include "QcTestUtil.h"
#include "server/core/QcServer.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"

#include <QElapsedTimer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

using namespace Qt::StringLiterals;

// A paused server keeps its connections (decision E-877): the tick loop stands still, but the
// transports send keep-alives on their own, so neither end drops the other while nothing else is
// said. Waits in real time (three idle timeouts of 1 s, long enough for a sanitizer build), hence in tests/timing.cmake.
class TestPauseKeepAlive : public QObject
{
    Q_OBJECT

private:
    static constexpr int timeoutMs = 15000;
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

private slots:
    void keepAliveHoldsAPausedClient()
    {
        QTcpServer listener;
        QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
        auto *socket = new QTcpSocket(&listener);
        socket->connectToHost(listener.serverAddress(), listener.serverPort());
        QVERIFY(listener.waitForNewConnection(timeoutMs) && socket->waitForConnected(timeoutMs));
        QTcpSocket *accepted = listener.nextPendingConnection();

        // The host's own player over a short-lived TCP link (the in-process transport has no timeout).
        const QcTcpTransport::Limits limits{.keepAliveMs = 100, .idleTimeoutMs = 1000};
        QcServerConfig config;
        config.viewDistance = 1;
        QcServer server(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QVERIFY(server.openWorld().has_value());
        server.addClient(std::make_unique<QcTcpTransport>(accepted, limits), QcServer::ClientOrigin::Local);
        QcRawClient client(std::make_unique<QcTcpTransport>(socket, limits));
        client.hello(u"Host"_s);
        QVERIFY(QTest::qWaitFor(
            [&] {
                QcTestUtil::ticks(server, 1);
                return client.playerList.size() == 1;
            },
            timeoutMs));

        client.connection.send(QcMsg::PauseRequest{true});
        // The pause begins once the player's view has arrived.
        QVERIFY(QTest::qWaitFor(
            [&] {
                QcTestUtil::ticks(server, 1);
                return client.pauses == QList<bool>{true};
            },
            timeoutMs));
        const quint64 paused = server.tickCount();

        // Three idle timeouts with the server's ticks running empty: both ends stay.
        QElapsedTimer waited;
        waited.start();
        while (waited.elapsed() < 3000) {
            QcTestUtil::ticks(server, 1);
            QTest::qWait(20);
        }
        QCOMPARE(server.tickCount(), paused);
        QCOMPARE(server.clientCount(), 1);
        QVERIFY(client.connection.isOpen());

        client.connection.send(QcMsg::PauseRequest{false});
        QVERIFY(QTest::qWaitFor(
            [&] {
                QcTestUtil::ticks(server, 1);
                return server.tickCount() > paused;
            },
            timeoutMs));
        QCOMPARE(client.pauses, (QList<bool>{true, false}));
    }
};

QTEST_GUILESS_MAIN(TestPauseKeepAlive)
#include "test_pause_keepalive.moc"
