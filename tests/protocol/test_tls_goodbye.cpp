// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/protocol/QcFrameReader.h"
#include "fixtures/QcTestTls.h"
#include "network/QcTcpTransport.h"
#include "network/tls/QcTlsBackend.h"
#include "network/tls/QcTlsListener.h"

#include <QPointer>
#include <QSignalSpy>
#include <QSslSocket>
#include <QTest>

#include <memory>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#else
#include <poll.h>
#include <sys/socket.h>
#endif

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 10'000;

// Waits up to timeoutMs until the system holds something for the socket to read (data, an end or a
// reset), without Qt's event loop: Qt's socket does not know of it yet.
bool waitReadable(qintptr descriptor)
{
#ifdef Q_OS_WIN
    WSAPOLLFD entry{static_cast<SOCKET>(descriptor), POLLRDNORM, 0};
    return ::WSAPoll(&entry, 1, timeoutMs) == 1;
#else
    pollfd entry{static_cast<int>(descriptor), POLLIN, 0};
    return ::poll(&entry, 1, timeoutMs) == 1;
#endif
}

// Resets the connection: the socket closes with a linger time of zero, so the system sends a reset.
void resetConnection(QAbstractSocket &socket)
{
    const linger now{1, 0};
#ifdef Q_OS_WIN
    // The socket API takes every option value as a byte pointer.
    ::setsockopt(static_cast<SOCKET>(socket.socketDescriptor()), SOL_SOCKET, SO_LINGER, reinterpret_cast<const char *>(&now), sizeof(now)); // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
#else
    ::setsockopt(static_cast<int>(socket.socketDescriptor()), SOL_SOCKET, SO_LINGER, &now, sizeof(now));
#endif
    socket.abort();
}

// A frame as it travels: its header, then its bytes.
QByteArray onTheWire(const QByteArray &frame)
{
    return QcFrameReader::header(frame.size()) + frame;
}

} // namespace

// The goodbye of a TLS connection (decision E-1076): Qt's TLS socket tells of written bytes from
// inside its own encryption, so over TLS every step of the goodbye runs from the event loop. It
// neither closes the socket inside that encryption nor ends the connection (FIN) before the last
// encrypted bytes reached the system. Everything runs on the loopback interface.
class TestTlsGoodbye : public QObject
{
    Q_OBJECT

private:
    // A TLS peer and the transport of the accepted side, both encrypted; no transport when that
    // failed.
    struct Link
    {
        std::unique_ptr<QSslSocket> peer;
        QPointer<QTcpSocket> socket;
        std::unique_ptr<QcTcpTransport> transport;
    };

    static Link connectedLink(QcTlsListener &listener)
    {
        Link link{std::make_unique<QSslSocket>(), {}, {}};
        link.peer->setPeerVerifyMode(QSslSocket::VerifyNone);
        link.peer->connectToHostEncrypted(u"127.0.0.1"_s, listener.serverPort());
        if (!QTest::qWaitFor([&listener] { return listener.hasPendingConnections(); }, timeoutMs)) {
            return {};
        }
        link.socket = listener.nextPendingConnection();
        QcTlsListener::startEncryption(link.socket.data());
        auto transport = std::make_unique<QcTcpTransport>(link.socket.data(), QcTcpTransport::Limits{.lingerMs = 5'000});
        const auto *accepted = qobject_cast<QSslSocket *>(link.socket.data());
        // Also until the accepted side handed everything of the handshake to the system: the tests
        // start from a socket whose buffers are empty.
        const auto settled = [&link, accepted] {
            return link.peer->isEncrypted() && accepted->isEncrypted() && accepted->bytesToWrite() == 0 && accepted->encryptedBytesToWrite() == 0;
        };
        if (!QTest::qWaitFor(settled, timeoutMs)) {
            return {};
        }
        link.transport = std::move(transport);
        return link;
    }

private slots:
    void initTestCase()
    {
        if constexpr (!QcTlsBackend::enabled) {
            QSKIP("built without TLS (QC_TLS=OFF)");
        }
    }

    // A peer that said goodbye and then reset the connection: the goodbye's end (FIN) fails, so it
    // closes the socket. Qt's TLS socket tells of the bytes it encrypted from inside the flush that
    // encrypts the last frames; the goodbye closes only from the event loop, after that flush has
    // left Qt's TLS state.
    void aGoodbyeAfterAResetEndsWithoutCrashing()
    {
        QcTlsListener listener(QcTestTls::credentials());
        QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
        Link link = connectedLink(listener);
        QVERIFY(link.transport);

        // The frame waits unencrypted in the socket's buffer until the goodbye flushes it.
        link.transport->sendFrame("Still on its way");
        link.transport->peerSaidGoodbye();
        resetConnection(*link.peer);
        QVERIFY(waitReadable(link.socket->socketDescriptor()));
        link.transport->close();
        QVERIFY(!link.transport->isOpen());
        QTRY_COMPARE_WITH_TIMEOUT(link.socket->state(), QAbstractSocket::UnconnectedState, timeoutMs);
        link.transport.reset();
        QTRY_VERIFY_WITH_TIMEOUT(link.socket.isNull(), timeoutMs);
    }

    // The server's way: the peer's frame and its reset arrive together, and the goodbye starts
    // while Qt hands that frame out from inside its decryption.
    void aGoodbyeFromAReceivedFrameAfterAResetEndsWithoutCrashing()
    {
        QcTlsListener listener(QcTestTls::credentials());
        QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
        Link link = connectedLink(listener);
        QVERIFY(link.transport);
        QcTcpTransport *transport = link.transport.get();
        QSignalSpy frames(transport, &QcTransport::frameReceived);
        connect(transport, &QcTransport::frameReceived, this, [transport] {
            transport->sendFrame("Answer");
            transport->peerSaidGoodbye();
            transport->close();
        });

        link.peer->write(onTheWire("Goodbye"));
        QVERIFY(link.peer->flush());
        resetConnection(*link.peer);
        QVERIFY(waitReadable(link.socket->socketDescriptor()));
        QTRY_COMPARE_WITH_TIMEOUT(link.socket->state(), QAbstractSocket::UnconnectedState, timeoutMs);
        // The goodbye began inside the decryption of that frame.
        QCOMPARE(frames.count(), 1);
        link.transport.reset();
        QTRY_VERIFY_WITH_TIMEOUT(link.socket.isNull(), timeoutMs);
    }

    // After the peer's goodbye the end (FIN) goes out at once, but only behind the frames still
    // waiting: the peer reads them before its socket closes, without an error.
    void aGoodbyeAfterThePeersSendsTheWaitingFrameBeforeTheEnd()
    {
        QcTlsListener listener(QcTestTls::credentials());
        QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
        Link link = connectedLink(listener);
        QVERIFY(link.transport);
        QSslSocket &peer = *link.peer;
        QByteArray received;
        QByteArray receivedAtTheEnd;
        QStringList errors;
        connect(&peer, &QIODevice::readyRead, this, [&] { received += peer.readAll(); });
        connect(&peer, &QAbstractSocket::stateChanged, this, [&](QAbstractSocket::SocketState state) {
            if (state == QAbstractSocket::UnconnectedState) {
                receivedAtTheEnd = received + peer.readAll();
            }
        });
        connect(&peer, &QAbstractSocket::errorOccurred, this, [&](QAbstractSocket::SocketError error) {
            if (error != QAbstractSocket::RemoteHostClosedError) {
                errors.append(peer.errorString());
            }
        });

        link.transport->sendFrame("x");
        link.transport->peerSaidGoodbye();
        link.transport->close();
        QTRY_COMPARE_WITH_TIMEOUT(peer.state(), QAbstractSocket::UnconnectedState, timeoutMs);
        QCOMPARE(receivedAtTheEnd, onTheWire("x"));
        QVERIFY2(errors.isEmpty(), qPrintable(errors.join(u"; "_s)));
    }
};

QTEST_GUILESS_MAIN(TestTlsGoodbye)
#include "test_tls_goodbye.moc"
