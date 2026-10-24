#include "clientcore/network/QcKnownServers.h"
#include "clientcore/network/QcNetworkClient.h"
#include "clientcore/session/QcClientSession.h"
#include "clientcore/tick/QcSessionLink.h"
#include "network/tls/QcTlsBackend.h"
#include "network/tls/QcTlsCredentials.h"
#include "network/tls/QcTlsListener.h"
#include "QcTestTls.h"
#include "QcTestUtil.h"
#include "server/core/QcServerConfig.h"
#include "worldgen/generators/QcTestTerrainGenerator.h"
#include "worlds/QcLocalServerHost.h"

#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 20'000;

} // namespace

// Joining a network game from the client (decision E-251, the API of the join menu): connect, log
// in, take over the session; refusals and unreachable servers come back as failures; the server's
// certificate is pinned at the first join and another one is refused (decision E-872).
class TestNetworkClient : public QObject
{
    Q_OBJECT

private:
    std::shared_ptr<const QcContentRegistry> m_content = QcTestUtil::loadContent();

    std::unique_ptr<QcLocalServerHost> host(QcServerConfig config = {}, const QcTlsCredentials &tls = QcTestTls::credentials(), quint16 port = 0,
                                            const QcConnectionLimits &limits = {})
    {
        config.viewDistance = 2;
        auto server = std::make_unique<QcLocalServerHost>(config, m_content, std::make_unique<QcTestTerrainGenerator>(m_content));
        QcNetworkConfig network;
        network.bindAddress = QHostAddress::LocalHost;
        network.port = port;
        network.tls = tls;
        network.connections = limits;
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
        // Every test server shows the same certificate; the pinning has tests of its own below.
        request.pinning = QcNetworkClient::Pinning::DisabledForTests;
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

    // The game's way to join: the session ticks on its own thread, and the TLS socket is made,
    // used and closed in that thread (decision E-872). A socket made in one thread and moved to
    // the tick thread shared its TLS state with the thread it was made in: joins hung, the server
    // read a garbled Hello, OpenSSL crashed (under TSan on every join). Many joins in a row.
    void tlsJoinsOnTheSessionThreadSucceedRepeatedly()
    {
        if (!QcSessionLink::threadWanted()) {
            QSKIP("the session thread is turned off (QTCRAFT_SESSION_THREAD=0)");
        }
        QTest::failOnWarning(QRegularExpression(u"another thread|cannot move|current thread is not"_s, QRegularExpression::CaseInsensitiveOption));
        constexpr int joins = 20;
        // One address joins more often than the flood guard lets it at once (decision E-254).
        QcConnectionLimits limits;
        limits.burst = joins;
        std::unique_ptr<QcLocalServerHost> server = host({}, QcTestTls::credentials(), 0, limits);
        QVERIFY(server);

        for (int i = 0; i < joins; ++i) {
            QcNetworkClient client;
            QSignalSpy joined(&client, &QcNetworkClient::joined);
            QSignalSpy failed(&client, &QcNetworkClient::failed);
            QcNetworkClient::Request threaded = request(server->listeningPort(), u"Ivy"_s);
            QCOMPARE(threaded.tickMode, QcSessionTickMode::Threaded);
            client.join(threaded);
            QVERIFY2(joined.wait(timeoutMs), qPrintable(u"join %1: %2"_s.arg(i).arg(failed.isEmpty() ? u"no answer"_s : failed.first().first().toString())));

            std::unique_ptr<QcClientSession> session = client.takeSession();
            QVERIFY(session);
            QTRY_VERIFY_WITH_TIMEOUT(!session->players().isEmpty() && session->players().first().name == u"Ivy"_s, timeoutMs);
            session->disconnectFromServer();
            session.reset();
            QTRY_VERIFY_WITH_TIMEOUT(server->runConsoleCommand(u"list"_s).join(u' ').contains(u"0 players online"_s), timeoutMs);
        }
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
        QcTlsListener silent(QcTestTls::credentials());
        QVERIFY(silent.listen(QHostAddress::LocalHost, 0));
        QcNetworkClient client;
        QSignalSpy joined(&client, &QcNetworkClient::joined);
        QSignalSpy failed(&client, &QcNetworkClient::failed);
        client.join(request(silent.serverPort(), u"Eve"_s));
        QTRY_VERIFY_WITH_TIMEOUT(silent.hasPendingConnections(), timeoutMs);
        QTcpSocket *peer = silent.nextPendingConnection();
        QVERIFY(peer);
        QcTlsListener::startEncryption(peer);
        QTRY_COMPARE_WITH_TIMEOUT(client.stage(), QcNetworkClient::Stage::LoggingIn, timeoutMs);
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

    // Trust on first use: the first join pins the certificate, the next ones find it; a pin of
    // another certificate refuses the join before the Hello, until the pin is forgotten.
    void pinsTheCertificateAndRefusesAnotherOne()
    {
        if constexpr (!QcTlsBackend::enabled) {
            QSKIP("built without TLS (QC_TLS=OFF)");
        }
        std::unique_ptr<QcLocalServerHost> server = host();
        QVERIFY(server);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString knownFile = directory.filePath(u"known-servers.json"_s);
        const QcServerAddress address{u"127.0.0.1"_s, server->listeningPort()};
        const QByteArray fingerprint = QcTlsFingerprint::of(QcTestTls::credentials().certificate);

        QcNetworkClient client;
        QSignalSpy joined(&client, &QcNetworkClient::joined);
        QSignalSpy failed(&client, &QcNetworkClient::failed);
        QSignalSpy changed(&client, &QcNetworkClient::certificateChanged);
        QcNetworkClient::Request first = request(address.port, u"Fay"_s);
        first.knownServersFile = knownFile;
        first.pinning = QcNetworkClient::Pinning::Check;
        client.join(first);
        QVERIFY(joined.wait(timeoutMs));
        QCOMPARE(client.serverFingerprint(), fingerprint);
        client.takeSession()->disconnectFromServer();
        QcKnownServers known(knownFile);
        QVERIFY(known.load());
        QCOMPARE(known.check(address, fingerprint), QcKnownServers::Trust::Known);
        QCOMPARE(QFile::permissions(knownFile) & (QFileDevice::ReadGroup | QFileDevice::ReadOther), QFileDevice::Permissions{});

        // The same address with a new certificate (a man in the middle would show his own).
        const QcResult<QcTlsCredentials> other = QcTlsCredentials::generate();
        QVERIFY2(other, qPrintable(other.error_or(QString())));
        const QByteArray otherFingerprint = QcTlsFingerprint::of(other->certificate);
        QVERIFY(otherFingerprint != fingerprint);
        QVERIFY(server->shutdown().has_value());
        server = host({}, *other, address.port);
        QVERIFY(server);
        QCOMPARE(server->listeningPort(), address.port);
        client.join(first);
        QVERIFY(failed.wait(timeoutMs));
        QCOMPARE(joined.count(), 1);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().first().toString(), u"127.0.0.1:%1"_s.arg(address.port));
        QCOMPARE(client.serverFingerprint(), otherFingerprint);
        const QString reason = failed.first().first().toString();
        QVERIFY2(reason.contains(u"another certificate"_s) && reason.contains(QcTlsFingerprint::display(fingerprint)) &&
                     reason.contains(QcTlsFingerprint::display(otherFingerprint)),
                 qPrintable(reason));
        QVERIFY(!client.takeSession());
        // The pin stays as it was.
        QVERIFY(known.load());
        QCOMPARE(known.find(address).value_or(QcKnownServers::Entry{}).fingerprint, fingerprint);

        // Forgotten, the new certificate is pinned at the next join.
        QVERIFY(known.forget(address));
        client.join(first);
        QVERIFY(joined.wait(timeoutMs));
        client.takeSession()->disconnectFromServer();
        QVERIFY(known.load());
        QCOMPARE(known.check(address, otherFingerprint), QcKnownServers::Trust::Known);
        QVERIFY(server->shutdown().has_value());
    }

    // Checking is the default: with TLS a join without a file for the pins fails instead of
    // connecting unchecked.
    void aJoinWithoutPinsFails()
    {
        if constexpr (!QcTlsBackend::enabled) {
            QSKIP("built without TLS (QC_TLS=OFF)");
        }
        QCOMPARE(QcNetworkClient::Request{}.pinning, QcNetworkClient::Pinning::Check);
        std::unique_ptr<QcLocalServerHost> server = host();
        QVERIFY(server);
        QcNetworkClient client;
        QSignalSpy joined(&client, &QcNetworkClient::joined);
        QSignalSpy failed(&client, &QcNetworkClient::failed);
        QcNetworkClient::Request unpinned = request(server->listeningPort(), u"Gil"_s);
        unpinned.pinning = QcNetworkClient::Pinning::Check;
        client.join(unpinned);
        QVERIFY(failed.wait(timeoutMs));
        QCOMPARE(joined.count(), 0);
        const QString reason = failed.first().first().toString();
        QVERIFY2(reason.contains(u"cannot check the server's certificate"_s), qPrintable(reason));
        QVERIFY(!client.takeSession());
        QVERIFY(server->shutdown().has_value());
    }

    // A server whose certificate does not match the pin gets not one byte of the client: the check
    // runs between the handshake and the session (no Hello, no name, no credentials).
    void nothingIsSentBeforeThePinCheck()
    {
        if constexpr (!QcTlsBackend::enabled) {
            QSKIP("built without TLS (QC_TLS=OFF)");
        }
        const QcResult<QcTlsCredentials> impostor = QcTlsCredentials::generate();
        QVERIFY2(impostor, qPrintable(impostor.error_or(QString())));
        QcTlsListener listener(*impostor);
        QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString knownFile = directory.filePath(u"known-servers.json"_s);
        const QcServerAddress address{u"127.0.0.1"_s, listener.serverPort()};
        QcKnownServers known(knownFile);
        QVERIFY(known.remember(address, QcTlsFingerprint::of(QcTestTls::credentials().certificate)));

        QcNetworkClient client;
        QSignalSpy failed(&client, &QcNetworkClient::failed);
        QSignalSpy changed(&client, &QcNetworkClient::certificateChanged);
        QcNetworkClient::Request pinned = request(address.port, u"Hal"_s, u"secret"_s);
        pinned.knownServersFile = knownFile;
        pinned.pinning = QcNetworkClient::Pinning::Check;
        client.join(pinned);
        QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), timeoutMs);
        QTcpSocket *peer = listener.nextPendingConnection();
        QVERIFY(peer);
        // Decrypted application data stays in the socket's buffer, also after the connection closed.
        QSignalSpy readable(peer, &QIODevice::readyRead);
        QcTlsListener::startEncryption(peer);

        QVERIFY(failed.wait(timeoutMs));
        QCOMPARE(changed.count(), 1);
        // The client closes the connection; until then and after, no application data came.
        QTRY_COMPARE_WITH_TIMEOUT(peer->state(), QAbstractSocket::UnconnectedState, timeoutMs);
        QCOMPARE(readable.count(), 0);
        QCOMPARE(peer->readAll().size(), 0);
        QVERIFY(!client.takeSession());
    }

    // The address as the pin's key, and a damaged file that is reported instead of overwritten.
    void knownServersFile()
    {
        QCOMPARE(QcKnownServers::keyOf({u"Example.ORG"_s, 25600}), u"example.org:25600"_s);
        QCOMPARE(QcKnownServers::keyOf(*QcServerAddress::parse(u"[::1]:7"_s)), u"[::1]:7"_s);
        // One IPv6 address written in several ways is one pin (the playtest runs over IPv6).
        QCOMPARE(QcKnownServers::keyOf(*QcServerAddress::parse(u"[2A02:0DB8:0::1]:25600"_s)), u"[2a02:db8::1]:25600"_s);
        QCOMPARE(QcKnownServers::keyOf(*QcServerAddress::parse(u"2a02:db8:0:0::1"_s)), u"[2a02:db8::1]:25600"_s);
        QCOMPARE(QcKnownServers::keyOf(*QcServerAddress::parse(u"[2a02:db8::1]"_s)), u"[2a02:db8::1]:25600"_s);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(u"known-servers.json"_s);
        QcKnownServers known(path);
        QVERIFY(known.load());
        QCOMPARE(known.check({u"a"_s, 1}, QByteArray(64, 'b')), QcKnownServers::Trust::New);
        QVERIFY(known.forget({u"a"_s, 1}));
        QVERIFY(!QFile::exists(path));

        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"format": 1, "servers": [{"address": "a:1", "fingerprint": "xyz"}]})");
        file.close();
        const QcStatus damaged = known.load();
        QVERIFY(!damaged);
        QVERIFY2(damaged.error().contains(u"damaged"_s), qPrintable(damaged.error()));
    }
};

QTEST_MAIN(TestNetworkClient)
#include "test_network_client.moc"
