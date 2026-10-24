#include "common/protocol/QcFrameReader.h"
#include "network/QcTcpConnector.h"
#include "network/QcTcpTransport.h"
#include "network/tls/QcTlsBackend.h"
#include "network/tls/QcTlsListener.h"
#include "QcTestTls.h"

#include <QElapsedTimer>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <memory>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 10'000;

// Collects what a transport receives.
struct Receiver
{
    explicit Receiver(QcTcpTransport &transport)
    {
        QObject::connect(&transport, &QcTransport::frameReceived, &transport, [this](const QByteArray &frame) { frames.append(frame); });
        QObject::connect(&transport, &QcTransport::closed, &transport, [this] { ++closed; });
    }

    QList<QByteArray> frames;
    int closed = 0;
};

} // namespace

// The TCP transport (decision E-251): frames in both directions, fragments, large frames, the end
// of a connection, keep-alive and timeout, slow peers, IPv4 and IPv6, and connecting. Everything
// runs on the loopback interface.
class TestTcpTransport : public QObject
{
    Q_OBJECT

private:
    QTcpServer m_listener;

    // A connected pair: the accepted socket and the client socket, both raw.
    std::pair<QTcpSocket *, QTcpSocket *> rawPair(const QHostAddress &address = QHostAddress::LocalHost)
    {
        if (!m_listener.isListening() && !m_listener.listen(address, 0)) {
            return {nullptr, nullptr};
        }
        auto *client = new QTcpSocket(this);
        client->connectToHost(m_listener.serverAddress(), m_listener.serverPort());
        if (!m_listener.waitForNewConnection(timeoutMs) || !client->waitForConnected(timeoutMs)) {
            return {nullptr, nullptr};
        }
        QTcpSocket *accepted = m_listener.nextPendingConnection();
        accepted->setParent(this);
        return {accepted, client};
    }

    // Two transports talking to each other.
    std::pair<std::unique_ptr<QcTcpTransport>, std::unique_ptr<QcTcpTransport>> transportPair(QcTcpTransport::Limits limits = {})
    {
        auto [accepted, client] = rawPair();
        if (!accepted) {
            return {};
        }
        return {std::make_unique<QcTcpTransport>(accepted, limits), std::make_unique<QcTcpTransport>(client, limits)};
    }

private slots:
    void cleanup()
    {
        m_listener.close();
    }

    void framesTravelBothWaysInOrder()
    {
        auto [server, client] = transportPair();
        QVERIFY(server && client);
        Receiver atServer(*server);
        Receiver atClient(*client);
        QVERIFY(server->peerAddress().startsWith(u"127.0.0.1:"_s));

        const QList<QByteArray> frames{QByteArray("hello"), QByteArray(3, '\0'), QByteArray(100'000, 'x'), QByteArray("bye")};
        for (const QByteArray &frame : frames) {
            client->sendFrame(frame);
            server->sendFrame(frame + "!");
        }
        QTRY_COMPARE_WITH_TIMEOUT(atServer.frames.size(), frames.size(), timeoutMs);
        QTRY_COMPARE_WITH_TIMEOUT(atClient.frames.size(), frames.size(), timeoutMs);
        QCOMPARE(atServer.frames, frames);
        for (qsizetype i = 0; i < frames.size(); ++i) {
            QCOMPARE(atClient.frames[i], frames[i] + "!");
        }
    }

    void aLargeFrameArrivesWhole()
    {
        auto [server, client] = transportPair();
        QVERIFY(server && client);
        Receiver atClient(*client);
        // A chunk-heavy message: 20 MB, below the 32 MB limit.
        QByteArray large(qsizetype{20} * 1024 * 1024, Qt::Uninitialized);
        for (qsizetype i = 0; i < large.size(); ++i) {
            large[i] = static_cast<char>(i * 7 % 251);
        }
        server->sendFrame(large);
        QTRY_COMPARE_WITH_TIMEOUT(atClient.frames.size(), 1, timeoutMs);
        QCOMPARE(atClient.frames.first(), large);
        QCOMPARE(atClient.closed, 0);
    }

    void fragmentsAreJoined()
    {
        auto [accepted, raw] = rawPair();
        QVERIFY(accepted);
        QcTcpTransport transport(accepted, {});
        Receiver received(transport);

        const QByteArray stream = QcFrameReader::header(5) + QByteArray("abcde") + QcFrameReader::header(0) + QcFrameReader::header(2) + "fg";
        // One byte at a time with the event loop in between: every split of header and payload.
        for (const char byte : stream) {
            raw->write(&byte, 1);
            raw->flush();
            QTest::qWait(1);
        }
        QTRY_COMPARE_WITH_TIMEOUT(received.frames.size(), 2, timeoutMs);
        QCOMPARE(received.frames, (QList<QByteArray>{"abcde", "fg"}));
    }

    void anOversizedFrameEndsTheConnection()
    {
        auto [accepted, raw] = rawPair();
        QVERIFY(accepted);
        QcTcpTransport transport(accepted, {.maxIncomingFrameBytes = 1024});
        Receiver received(transport);
        QSignalSpy peerClosed(raw, &QTcpSocket::disconnected);

        raw->write(QcFrameReader::header(1025) + QByteArray(1025, 'x'));
        QTRY_COMPARE_WITH_TIMEOUT(received.closed, 1, timeoutMs);
        QVERIFY(received.frames.isEmpty());
        QVERIFY(!transport.isOpen());
        QVERIFY(transport.closeReason().contains(u"too large"_s));
        QTRY_COMPARE_WITH_TIMEOUT(peerClosed.count(), 1, timeoutMs);
    }

    void anOversizedFrameBehindAnotherEndsTheConnectionAtOnce()
    {
        auto [accepted, raw] = rawPair();
        QVERIFY(accepted);
        QcTcpTransport transport(accepted, {.maxIncomingFrameBytes = 1024});
        Receiver received(transport);

        // One write: a good frame, then only the header of a frame beyond the limit. Nothing else
        // follows, so no later read could notice it.
        raw->write(QcFrameReader::header(3) + QByteArray("abc") + QcFrameReader::header(1025));
        raw->flush();
        QTRY_COMPARE_WITH_TIMEOUT(received.closed, 1, 2'000);
        QCOMPARE(received.frames, QList<QByteArray>{"abc"});
        QVERIFY(transport.closeReason().contains(u"too large"_s));
    }

    void closingDeliversWhatWasSentBefore()
    {
        auto [server, client] = transportPair();
        QVERIFY(server && client);
        Receiver atClient(*client);
        // The reason of a kick goes out even when the transport is destroyed right after.
        server->sendFrame("reason");
        server->close();
        QVERIFY(!server->isOpen());
        server.reset();

        QTRY_COMPARE_WITH_TIMEOUT(atClient.closed, 1, timeoutMs);
        QCOMPARE(atClient.frames, QList<QByteArray>{"reason"});
        QVERIFY(!client->isOpen());
        QVERIFY(client->closeReason().contains(u"closed the connection"_s));
        // Sending on a closed transport does nothing.
        client->sendFrame("late");
    }

    void destroyingWithAStuckPeerDoesNotWait()
    {
        auto [accepted, raw] = rawPair();
        QVERIFY(accepted);
        // The raw peer reads nothing, so 16 MB stay queued at the transport.
        raw->setReadBufferSize(1);
        QPointer<QTcpSocket> socket(accepted);
        auto transport = std::make_unique<QcTcpTransport>(accepted, QcTcpTransport::Limits{.lingerMs = 300});
        transport->sendFrame(QByteArray(qsizetype{16} * 1024 * 1024, 'x'));
        QVERIFY(transport->pendingBytes() > 0);
        QSignalSpy peerClosed(raw, &QTcpSocket::disconnected);

        QElapsedTimer timer;
        timer.start();
        transport.reset();
        // The old destructor waited the whole linger time (200 ms) in the server's tick.
        QVERIFY2(timer.elapsed() < 100, qPrintable(QString::number(timer.elapsed())));
        QVERIFY(!socket.isNull());

        // The socket goes after the linger time and the peer sees the end.
        QTRY_VERIFY_WITH_TIMEOUT(socket.isNull(), timeoutMs);
        QVERIFY(timer.elapsed() >= 250);
        raw->setReadBufferSize(0);
        QTRY_VERIFY_WITH_TIMEOUT(peerClosed.count() > 0 || raw->state() == QAbstractSocket::UnconnectedState, timeoutMs);
    }

    void closedIsAnnouncedOnceAndLater()
    {
        auto [server, client] = transportPair();
        QVERIFY(server && client);
        Receiver atServer(*server);
        server->close();
        server->close();
        QCOMPARE(atServer.closed, 0);
        QTRY_COMPARE_WITH_TIMEOUT(atServer.closed, 1, timeoutMs);
        QTest::qWait(50);
        QCOMPARE(atServer.closed, 1);
    }

    void keepAlivesHoldAQuietConnection()
    {
        const QcTcpTransport::Limits limits{.keepAliveMs = 40, .idleTimeoutMs = 300};
        auto [server, client] = transportPair(limits);
        QVERIFY(server && client);
        Receiver atServer(*server);
        Receiver atClient(*client);

        // Three idle timeouts without any frame: the keep-alives hold it, and none is delivered.
        QTest::qWait(900);
        QVERIFY(server->isOpen() && client->isOpen());
        QCOMPARE(atServer.closed + atClient.closed, 0);
        QVERIFY(atServer.frames.isEmpty() && atClient.frames.isEmpty());
    }

    void aSilentPeerTimesOut()
    {
        auto [accepted, raw] = rawPair();
        QVERIFY(accepted);
        QcTcpTransport transport(accepted, {.keepAliveMs = 50, .idleTimeoutMs = 300});
        Receiver received(transport);
        QElapsedTimer timer;
        timer.start();

        // The raw peer never sends anything, not even keep-alives.
        QTRY_COMPARE_WITH_TIMEOUT(received.closed, 1, timeoutMs);
        QVERIFY(timer.elapsed() >= 290);
        QVERIFY(transport.closeReason().contains(u"timed out"_s));
        // It got keep-alives meanwhile: empty frames.
        QVERIFY(raw->bytesAvailable() >= QcFrameReader::headerBytes || raw->state() != QAbstractSocket::ConnectedState);
    }

    void aSlowPeerIsCutOff()
    {
        auto [accepted, raw] = rawPair();
        QVERIFY(accepted);
        // The raw peer reads nothing; the transport holds at most 1 MB.
        raw->setReadBufferSize(1);
        QcTcpTransport transport(accepted, {.maxPendingBytes = qint64{1024} * 1024});
        Receiver received(transport);
        const QByteArray frame(qsizetype{256} * 1024, 'x');
        for (int i = 0; i < 256 && transport.isOpen(); ++i) {
            transport.sendFrame(frame);
        }
        QVERIFY(!transport.isOpen());
        QVERIFY(transport.closeReason().contains(u"too slow"_s));
        QTRY_COMPARE_WITH_TIMEOUT(received.closed, 1, timeoutMs);
    }

    // Over TLS (decision E-872): the connector hands the transport over after the handshake, with
    // the fingerprint of the server's certificate; frames go both ways.
    void connectsOverIpv4AndIpv6()
    {
        for (const QHostAddress &address : {QHostAddress(QHostAddress::LocalHost), QHostAddress(QHostAddress::LocalHostIPv6)}) {
            QcTlsListener listener(QcTestTls::credentials());
            if (!listener.listen(address, 0)) {
                QVERIFY2(address.protocol() == QAbstractSocket::IPv6Protocol, qPrintable(listener.errorString()));
                qWarning("No IPv6 loopback here: %s", qPrintable(listener.errorString()));
                continue;
            }

            QcTcpConnector connector;
            QSignalSpy connected(&connector, &QcTcpConnector::connected);
            const QString text = address.protocol() == QAbstractSocket::IPv6Protocol ? u"[::1]:%1"_s : u"127.0.0.1:%1"_s;
            connector.connectTo(*QcServerAddress::parse(text.arg(listener.serverPort())));
            QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), timeoutMs);
            QTcpSocket *accepted = listener.nextPendingConnection();
            QcTlsListener::startEncryption(accepted);
            QcTcpTransport server(accepted, {});
            Receiver atServer(server);
            server.sendFrame("welcome");

            QVERIFY(connected.count() == 1 || connected.wait(timeoutMs));
            // Without TLS (QC_TLS=OFF) there is no certificate and nothing to pin.
            QCOMPARE(connector.peerFingerprint(), QcTlsBackend::enabled ? QcTlsFingerprint::of(QcTestTls::credentials().certificate) : QByteArray());
            std::unique_ptr<QcTcpTransport> client = connector.takeTransport();
            QVERIFY(client && client->isOpen());
            QVERIFY(!connector.takeTransport());
            Receiver atClient(*client);
            client->sendFrame("over " + address.toString().toLatin1());
            QTRY_COMPARE_WITH_TIMEOUT(atServer.frames.size(), 1, timeoutMs);
            QCOMPARE(atServer.frames.first(), "over " + address.toString().toLatin1());
            QTRY_COMPARE_WITH_TIMEOUT(atClient.frames.size(), 1, timeoutMs);
            QCOMPARE(atClient.frames.first(), QByteArray("welcome"));
        }
    }

    // The dedicated server's default (QHostAddress::Any) takes IPv4 and IPv6 on one port, and a host
    // name with an IPv6 address (AAAA, here "localhost" as ::1) reaches it; each with a handshake.
    void aDualStackListenerTakesIpv4Ipv6AndNames()
    {
        QcTlsListener listener(QcTestTls::credentials());
        QVERIFY2(listener.listen(QHostAddress::Any, 0), qPrintable(listener.errorString()));
        QStringList targets{u"127.0.0.1:%1"_s, u"localhost:%1"_s};
        if (QTcpServer probe; probe.listen(QHostAddress::LocalHostIPv6, 0)) {
            targets.append(u"[::1]:%1"_s);
        } else {
            qWarning("No IPv6 loopback here: %s", qPrintable(probe.errorString()));
        }

        for (const QString &target : std::as_const(targets)) {
            QcTcpConnector connector;
            QSignalSpy connected(&connector, &QcTcpConnector::connected);
            connector.connectTo(*QcServerAddress::parse(target.arg(listener.serverPort())));
            QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), timeoutMs);
            QTcpSocket *accepted = listener.nextPendingConnection();
            QcTlsListener::startEncryption(accepted);
            QcTcpTransport server(accepted, {});
            QVERIFY2(connected.count() == 1 || connected.wait(timeoutMs), qPrintable(target));
            QVERIFY(QcTlsBackend::enabled == !connector.peerFingerprint().isEmpty());
            qInfo().noquote() << u"%1 arrived from %2"_s.arg(target.arg(listener.serverPort()), server.peerAddress());
        }
    }

    // A server of a version before TLS takes the handshake for a frame too large and hangs up:
    // the player learns that no encrypted connection came about.
    void aServerWithoutTlsFails()
    {
        if constexpr (!QcTlsBackend::enabled) {
            QSKIP("built without TLS (QC_TLS=OFF)");
        }
        QTcpServer listener;
        QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
        QcTcpConnector connector;
        QSignalSpy failed(&connector, &QcTcpConnector::failed);
        connector.connectTo({u"127.0.0.1"_s, listener.serverPort()});
        QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), timeoutMs);
        auto old = std::make_unique<QcTcpTransport>(listener.nextPendingConnection(), QcTcpTransport::Limits{.maxIncomingFrameBytes = qint64{256} * 1024});

        QVERIFY(failed.count() == 1 || failed.wait(timeoutMs));
        QVERIFY2(failed.first().first().toString().contains(u"encrypted connection"_s), qPrintable(failed.first().first().toString()));
        QVERIFY(!connector.takeTransport());
        QTRY_VERIFY_WITH_TIMEOUT(!old->isOpen(), timeoutMs);
    }

    void aRefusedConnectionFails()
    {
        QTcpServer listener;
        QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
        const quint16 port = listener.serverPort();
        listener.close();

        QcTcpConnector connector;
        QSignalSpy failed(&connector, &QcTcpConnector::failed);
        QSignalSpy connected(&connector, &QcTcpConnector::connected);
        connector.connectTo({u"127.0.0.1"_s, port});
        QVERIFY(failed.wait(timeoutMs));
        QCOMPARE(connected.count(), 0);
        QVERIFY(!failed.first().first().toString().isEmpty());
        QVERIFY(!connector.takeTransport());
    }
};

QTEST_GUILESS_MAIN(TestTcpTransport)
#include "test_tcp_transport.moc"
