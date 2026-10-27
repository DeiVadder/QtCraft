#include "common/protocol/QcConnection.h"
#include "common/protocol/QcFrameReader.h"
#include "common/protocol/QcInProcessTransport.h"
#include "network/QcTcpConnector.h"
#include "network/QcTcpTransport.h"
#include "network/tls/QcTlsBackend.h"
#include "network/tls/QcTlsListener.h"
#include "QcTestTls.h"

#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QPointer>
#include <QSignalSpy>
#include <QSslSocket>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include <array>
#include <memory>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

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

// A peer on the system's own socket calls: unlike QTcpSocket (which reports a reset like an end)
// it tells a clean end (the other side's FIN) from a reset (decision E-904).
class NativePeer
{
public:
    enum class Read { Nothing, Data, End, Reset };

    explicit NativePeer(quint16 port)
        : m_socket(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP))
    {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        // The socket API takes every address family through sockaddr.
        m_connected = ::connect(m_socket, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == 0; // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
#ifdef Q_OS_WIN
        u_long nonBlocking = 1;
        ::ioctlsocket(m_socket, FIONBIO, &nonBlocking);
#else
#ifdef SO_NOSIGPIPE
        const int on = 1;
        ::setsockopt(m_socket, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
        ::fcntl(m_socket, F_SETFL, ::fcntl(m_socket, F_GETFL) | O_NONBLOCK);
#endif
    }
    ~NativePeer() { close(); }
    Q_DISABLE_COPY_MOVE(NativePeer)

    bool isConnected() const { return m_connected; }

    // Sends without blocking; what the system does not take is dropped (a closed or reset socket).
    void send(const QByteArray &bytes) const
    {
#ifdef Q_OS_WIN
        ::send(m_socket, bytes.constData(), static_cast<int>(bytes.size()), 0);
#elif defined(MSG_NOSIGNAL)
        ::send(m_socket, bytes.constData(), static_cast<size_t>(bytes.size()), MSG_NOSIGNAL);
#else
        ::send(m_socket, bytes.constData(), static_cast<size_t>(bytes.size()), 0);
#endif
    }

    // Reads what arrived (kept in received()) and tells what the read saw.
    Read read()
    {
        std::array<char, 4096> buffer{};
#ifdef Q_OS_WIN
        const int n = ::recv(m_socket, buffer.data(), static_cast<int>(buffer.size()), 0);
        const bool wouldBlock = n < 0 && WSAGetLastError() == WSAEWOULDBLOCK;
#else
        const auto n = ::recv(m_socket, buffer.data(), buffer.size(), 0);
        const bool wouldBlock = n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);
#endif
        if (n > 0) {
            m_received.append(buffer.data(), n);
            return Read::Data;
        }
        if (n == 0) {
            return Read::End;
        }
        return wouldBlock ? Read::Nothing : Read::Reset;
    }

    const QByteArray &received() const { return m_received; }

    void close()
    {
        if (!m_closed) {
            m_closed = true;
#ifdef Q_OS_WIN
            ::closesocket(m_socket);
#else
            ::close(m_socket);
#endif
        }
    }

private:
#ifdef Q_OS_WIN
    SOCKET m_socket = INVALID_SOCKET;
#else
    int m_socket = -1;
#endif
    QByteArray m_received;
    bool m_connected = false;
    bool m_closed = false;
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

    // The goodbye (decision E-904): a peer that goes on sending, like a client with its input of
    // every tick, still reads the last frame and then a clean end. Closing at once would answer
    // that input with a reset, which makes a Windows peer throw away the frame it had not read.
    void aPeerThatKeepsSendingReadsTheGoodbyeAndACleanEnd()
    {
        QVERIFY(m_listener.listen(QHostAddress::LocalHost, 0));
        NativePeer peer(m_listener.serverPort());
        QVERIFY(peer.isConnected());
        QVERIFY(m_listener.waitForNewConnection(timeoutMs));
        QPointer<QTcpSocket> socket(m_listener.nextPendingConnection());
        auto transport = std::make_unique<QcTcpTransport>(socket.data(), QcTcpTransport::Limits{.lingerMs = 5'000});
        const QByteArray keepAlive = QcFrameReader::header(0);
        // Input is on its way when the transport says goodbye.
        peer.send(keepAlive.repeated(16));
        const QByteArray reason("The server stopped");
        transport->sendFrame(reason);
        transport.reset();

        // The peer sends on for a while before it reads (input already on its way).
        QElapsedTimer sending;
        sending.start();
        while (!sending.hasExpired(100)) {
            peer.send(keepAlive);
            QTest::qWait(2);
        }
        NativePeer::Read last = NativePeer::Read::Nothing;
        const QDeadlineTimer deadline(timeoutMs);
        while ((last == NativePeer::Read::Nothing || last == NativePeer::Read::Data) && !deadline.hasExpired()) {
            QTest::qWait(1);
            last = peer.read();
        }
        QCOMPARE(peer.received(), QcFrameReader::header(reason.size()) + reason);
        QCOMPARE(last, NativePeer::Read::End);

        // The goodbye ends when the peer closes too, long before the linger time.
        QVERIFY(!socket.isNull());
        QElapsedTimer closing;
        closing.start();
        peer.close();
        QTRY_VERIFY_WITH_TIMEOUT(socket.isNull(), timeoutMs);
        QVERIFY2(closing.elapsed() < 2'000, qPrintable(QString::number(closing.elapsed())));
    }

    // Over TLS the goodbye waits until the last frame is encrypted and handed to the system; the
    // peer reads it and its close ends the goodbye, long before the end of the server's side would
    // come (decision E-904).
    void aTlsPeerReadsTheGoodbyeAndACleanEnd()
    {
        if constexpr (!QcTlsBackend::enabled) {
            QSKIP("built without TLS (QC_TLS=OFF)");
        }
        QcTlsListener listener(QcTestTls::credentials());
        QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
        QSslSocket peer;
        peer.setPeerVerifyMode(QSslSocket::VerifyNone);
        QByteArray received;
        QStringList errors;
        connect(&peer, &QIODevice::readyRead, this, [&] { received += peer.readAll(); });
        connect(&peer, &QAbstractSocket::errorOccurred, this, [&](QAbstractSocket::SocketError error) {
            if (error != QAbstractSocket::RemoteHostClosedError) {
                errors.append(peer.errorString());
            }
        });
        peer.connectToHostEncrypted(u"127.0.0.1"_s, listener.serverPort());
        QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), timeoutMs);
        QPointer<QTcpSocket> socket(listener.nextPendingConnection());
        QcTlsListener::startEncryption(socket.data());
        auto transport = std::make_unique<QcTcpTransport>(socket.data(), QcTcpTransport::Limits{.lingerMs = 5'000});
        QTRY_VERIFY_WITH_TIMEOUT(peer.isEncrypted(), timeoutMs);

        const QByteArray keepAlive = QcFrameReader::header(0);
        peer.write(keepAlive.repeated(16));
        const QByteArray reason("The server stopped");
        transport->sendFrame(reason);
        QElapsedTimer closing;
        closing.start();
        transport.reset();
        // The peer sends until the frame is there, then closes like a client after a Disconnect.
        const QByteArray expected = QcFrameReader::header(reason.size()) + reason;
        const QDeadlineTimer deadline(timeoutMs);
        while (received.size() < expected.size() && peer.state() == QAbstractSocket::ConnectedState && !deadline.hasExpired()) {
            peer.write(keepAlive);
            QTest::qWait(2);
        }
        QCOMPARE(received, expected);
        peer.disconnectFromHost();
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(u"; "_s)));
        QTRY_VERIFY_WITH_TIMEOUT(socket.isNull(), timeoutMs);
        QVERIFY2(closing.elapsed() < 2'000, qPrintable(QString::number(closing.elapsed())));
    }

    // A TLS peer that does not close by itself gets the end when the linger time ran out, behind the
    // frame it read before; when the peer left first, the end goes out at once (decision E-904).
    void aSilentTlsPeerGetsTheEndAfterTheLingerTime()
    {
        if constexpr (!QcTlsBackend::enabled) {
            QSKIP("built without TLS (QC_TLS=OFF)");
        }
        QcTlsListener listener(QcTestTls::credentials());
        QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
        for (const bool peerLeft : {false, true}) {
            QSslSocket peer;
            peer.setPeerVerifyMode(QSslSocket::VerifyNone);
            QByteArray received;
            connect(&peer, &QIODevice::readyRead, this, [&] { received += peer.readAll(); });
            peer.connectToHostEncrypted(u"127.0.0.1"_s, listener.serverPort());
            QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections(), timeoutMs);
            QPointer<QTcpSocket> socket(listener.nextPendingConnection());
            QcTlsListener::startEncryption(socket.data());
            const int lingerMs = peerLeft ? 5'000 : 1'000;
            auto transport = std::make_unique<QcTcpTransport>(socket.data(), QcTcpTransport::Limits{.lingerMs = lingerMs});
            QTRY_VERIFY_WITH_TIMEOUT(peer.isEncrypted(), timeoutMs);

            // A side whose peer left first sends nothing more, only its end.
            const QByteArray frame = peerLeft ? QByteArray() : QByteArray("Kicked");
            if (peerLeft) {
                transport->peerSaidGoodbye();
            } else {
                transport->sendFrame(frame);
            }
            QElapsedTimer closing;
            closing.start();
            transport.reset();
            QTRY_COMPARE_WITH_TIMEOUT(peer.state(), QAbstractSocket::UnconnectedState, timeoutMs);
            const qint64 elapsed = closing.elapsed();
            if (peerLeft) {
                QVERIFY2(elapsed < 2'000, qPrintable(QString::number(elapsed)));
                QVERIFY(received.isEmpty());
            } else {
                QVERIFY2(elapsed >= 900, qPrintable(QString::number(elapsed)));
                QCOMPARE(received, QcFrameReader::header(frame.size()) + frame);
            }
            QTRY_VERIFY_WITH_TIMEOUT(socket.isNull(), timeoutMs);
        }
    }

    // After the other end's Disconnect a connection sends nothing more (decision E-904).
    void aConnectionIsSilentAfterTheGoodbye()
    {
        auto [atServer, atClient] = QcInProcessTransport::createPair();
        int arrived = 0;
        QcConnection server(std::move(atServer), QcProtocol::Peer::Client, [&arrived](QcMessage &&) { ++arrived; });
        QcConnection client(std::move(atClient), QcProtocol::Peer::Server, [](QcMessage &&) {});
        client.send(QcMsg::ChatInput{u"before"_s});
        QTRY_COMPARE_WITH_TIMEOUT(arrived, 1, timeoutMs);

        server.send(QcMsg::Disconnect{u"The server stopped"_s, {}, {}});
        const quint64 goodbyeBytes = server.traffic().sent;
        QVERIFY(goodbyeBytes > 0);
        QTRY_COMPARE_WITH_TIMEOUT(client.traffic().received, goodbyeBytes, timeoutMs);
        const quint64 sent = client.traffic().sent;
        client.send(QcMsg::ChatInput{u"after"_s});
        QCOMPARE(client.traffic().sent, sent);
        QTest::qWait(20);
        QCOMPARE(arrived, 1);
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
        // Destroying the transport never waits for the goodbye (the server's tick goes on).
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
