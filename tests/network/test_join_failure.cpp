// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Join failures that explain themselves (decision E-964): socket errors map to the step that
// failed, each step has a reason and a hint, and the connector reports the addresses it tried.

#include "fixtures/QcTestTls.h"
#include "network/QcJoinFailure.h"
#include "network/QcTcpConnector.h"
#include "network/tls/QcTlsBackend.h"
#include "network/tls/QcTlsListener.h"

#include <QMetaEnum>
#include <QSet>
#include <QHostInfo>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTest>

#include <optional>

using namespace Qt::StringLiterals;

namespace {

constexpr int timeoutMs = 10'000;

using Kind = QcJoinFailure::Kind;
using Step = QcJoinFailure::Step;

QcJoinFailure failureAt(Kind kind, const QString &address, const QList<QHostAddress> &tried)
{
    QcJoinFailure failure = QcJoinFailure::of(kind, *QcServerAddress::parse(address), u"system text"_s);
    failure.tried = tried;
    return failure;
}

QcJoinFailure firstFailure(const QSignalSpy &spy)
{
    return spy.first().first().value<QcJoinFailure>();
}

// Connects to `address` with the time limit `limitMs` and returns the failure the connector
// reports; std::nullopt when none comes.
std::optional<QcJoinFailure> failureJoining(const QcServerAddress &address, int limitMs = timeoutMs)
{
    QcTcpConnector connector;
    QSignalSpy failed(&connector, &QcTcpConnector::failed);
    connector.connectTo(address, limitMs);
    // The connector's own limit plus room for a loaded machine.
    if (!failed.wait(limitMs + timeoutMs)) {
        return std::nullopt;
    }
    return firstFailure(failed);
}

} // namespace

class TestJoinFailure : public QObject
{
    Q_OBJECT

private slots:
    void socketErrorsMapToTheirStep_data()
    {
        QTest::addColumn<QAbstractSocket::SocketError>("error");
        QTest::addColumn<Step>("step");
        QTest::addColumn<Kind>("kind");

        QTest::newRow("lookup") << QAbstractSocket::HostNotFoundError << Step::Lookup << Kind::LookupFailed;
        QTest::newRow("lookup timeout") << QAbstractSocket::SocketTimeoutError << Step::Lookup << Kind::LookupTimedOut;
        QTest::newRow("refused") << QAbstractSocket::ConnectionRefusedError << Step::Connecting << Kind::Refused;
        QTest::newRow("timeout") << QAbstractSocket::SocketTimeoutError << Step::Connecting << Kind::TimedOut;
        // ENETUNREACH/EHOSTUNREACH (an IPv6 address without IPv6 route) and a missing family.
        QTest::newRow("unreachable") << QAbstractSocket::NetworkError << Step::Connecting << Kind::Unreachable;
        QTest::newRow("no family") << QAbstractSocket::UnsupportedSocketOperationError << Step::Connecting << Kind::Unreachable;
        QTest::newRow("access") << QAbstractSocket::SocketAccessError << Step::Connecting << Kind::Blocked;
        QTest::newRow("closed") << QAbstractSocket::RemoteHostClosedError << Step::Connecting << Kind::ServerClosed;
        QTest::newRow("other") << QAbstractSocket::UnknownSocketError << Step::Connecting << Kind::Other;
        QTest::newRow("old server") << QAbstractSocket::RemoteHostClosedError << Step::Handshake << Kind::OtherVersion;
        QTest::newRow("handshake") << QAbstractSocket::SslHandshakeFailedError << Step::Handshake << Kind::OtherVersion;
        QTest::newRow("silent server") << QAbstractSocket::SocketTimeoutError << Step::Handshake << Kind::NoAnswer;
        QTest::newRow("tls other") << QAbstractSocket::SslInternalError << Step::Handshake << Kind::TlsFailed;
    }

    void socketErrorsMapToTheirStep()
    {
        QFETCH(QAbstractSocket::SocketError, error);
        QFETCH(Step, step);
        QFETCH(Kind, kind);
        QCOMPARE(QcJoinFailure::kindOf(error, step), kind);
    }

    // Each step of the connection has a reason of its own and a hint; the kinds whose reason comes
    // from elsewhere take the detail.
    void everyConnectionStepHasAReasonAndAHint()
    {
        const QList<QHostAddress> v4{QHostAddress(u"203.0.113.5"_s)};
        QSet<QString> reasons;
        for (const Kind kind : {Kind::LookupFailed, Kind::LookupTimedOut, Kind::TimedOut, Kind::Refused, Kind::Unreachable, Kind::Blocked, Kind::NoAnswer,
                                Kind::OtherVersion, Kind::TlsFailed, Kind::NoCertificate, Kind::ServerClosed}) {
            const QcJoinFailure failure = failureAt(kind, u"203.0.113.5"_s, v4);
            QVERIFY2(!failure.reason.isEmpty() && failure.reason != failure.detail, QMetaEnum::fromType<Kind>().valueToKey(static_cast<int>(kind)));
            QVERIFY(!failure.hint().isEmpty());
            reasons.insert(failure.reason);
        }
        QCOMPARE(reasons.size(), 11);
        QCOMPARE(QcJoinFailure::of(Kind::Other, {}, u"system text"_s).reason, u"system text"_s);
        QVERIFY(QcJoinFailure::of(Kind::LoginRefused, {}, {}).hint().isEmpty());
        QVERIFY(QcJoinFailure::reasonFor(Kind::LookupFailed, u"play.example.org"_s).contains(u"play.example.org"_s));
    }

    // Only when every address tried was IPv6 does the hint point at IPv6 on the player's side.
    void theIpv6HintNeedsOnlyIpv6Addresses()
    {
        const QHostAddress v6(u"2001:db8::1"_s);
        const QHostAddress v4(u"203.0.113.5"_s);
        for (const Kind kind : {Kind::Unreachable, Kind::TimedOut}) {
            QVERIFY(failureAt(kind, u"[2001:db8::1]:25600"_s, {v6}).hint().contains(u"IPv4"_s));
            QVERIFY(!failureAt(kind, u"play.example.org"_s, {v6, v4}).hint().contains(u"IPv6"_s));
            QVERIFY(!failureAt(kind, u"203.0.113.5"_s, {v4}).hint().contains(u"IPv6"_s));
        }
        QVERIFY(failureAt(Kind::Unreachable, u"[2001:db8::1]"_s, {v6}).triedOnlyIpv6());
        QVERIFY(!failureAt(Kind::LookupFailed, u"play.example.org"_s, {}).triedOnlyIpv6());
    }

    void theTriedLineNamesAddressesAndFamilies()
    {
        const QHostAddress v6(u"2001:db8::1"_s);
        const QHostAddress v4(u"203.0.113.5"_s);
        QCOMPARE(failureAt(Kind::TimedOut, u"203.0.113.5:4000"_s, {v4}).triedText(), u"Tried 203.0.113.5:4000 (IPv4)"_s);
        QCOMPARE(failureAt(Kind::TimedOut, u"play.example.org"_s, {v6, v4}).triedText(),
                 u"Tried play.example.org at [2001:db8::1]:25600 (IPv6), 203.0.113.5:25600 (IPv4)"_s);
        QCOMPARE(failureAt(Kind::LookupFailed, u"play.example.org:4000"_s, {}).triedText(), u"Address: play.example.org:4000"_s);
        QVERIFY(QcJoinFailure::of(Kind::Other, {}, u"no address"_s).triedText().isEmpty());

        QcJoinFailure failure = failureAt(Kind::Unreachable, u"[2001:db8::1]"_s, {v6});
        failure.steps = {u"2001:db8::1: Network unreachable"_s};
        const QString log = failure.logText();
        for (const QString &part : {u"[2001:db8::1]:25600"_s, u"Unreachable"_s, u"(IPv6)"_s, u"Network unreachable"_s, u"system text"_s}) {
            QVERIFY2(log.contains(part), qPrintable(log));
        }
    }

    // A real refusal: a closed local port.
    void aClosedPortIsRefused()
    {
        QTcpServer listener;
        QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
        const quint16 port = listener.serverPort();
        listener.close();

        const std::optional<QcJoinFailure> joined = failureJoining({u"127.0.0.1"_s, port});
        QVERIFY(joined);
        const QcJoinFailure &failure = *joined;
        QCOMPARE(failure.kind, Kind::Refused);
        QCOMPARE(failure.tried, QList<QHostAddress>{QHostAddress(QHostAddress::LocalHost)});
        QCOMPARE(failure.steps.size(), 1);
        QVERIFY2(failure.logText().contains(u"127.0.0.1:%1"_s.arg(port)), qPrintable(failure.logText()));
    }

    // A listener that takes the connection but never answers the handshake runs into the time
    // limit (2 s, so the TCP connection surely stands) as "no answer", not as a timed-out connection.
    void aSilentServerRunsOutOfTime()
    {
        if constexpr (!QcTlsBackend::enabled) {
            QSKIP("built without TLS (QC_TLS=OFF): a plain connection stands at once");
        }
        QTcpServer listener;
        QVERIFY(listener.listen(QHostAddress::LocalHost, 0));
        const std::optional<QcJoinFailure> failure = failureJoining({u"127.0.0.1"_s, listener.serverPort()}, 2'000);
        QVERIFY(failure);
        QCOMPARE(failure->kind, Kind::NoAnswer);
    }

    // "localhost" may stand for ::1 and 127.0.0.1: with a listener on IPv4 only, a refused IPv6
    // address is followed by the next one.
    void aNameTriesItsNextAddress()
    {
        const QList<QHostAddress> addresses = QHostInfo::fromName(u"localhost"_s).addresses();
        const qsizetype v6 = addresses.indexOf(QHostAddress(QHostAddress::LocalHostIPv6));
        const qsizetype v4 = addresses.indexOf(QHostAddress(QHostAddress::LocalHost));
        if (v6 < 0 || v4 < 0 || v6 > v4) {
            QSKIP("localhost does not stand for ::1 before 127.0.0.1 here");
        }
        // The refused ::1 is noted (QcTcpConnector::noteStep) before 127.0.0.1 is tried.
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression(u"^Connecting to ::1 port \\d+ failed"_s));
        QcTlsListener listener(QcTestTls::credentials());
        QVERIFY2(listener.listen(QHostAddress::LocalHost, 0), qPrintable(listener.errorString()));
        QcTcpConnector connector;
        QSignalSpy connected(&connector, &QcTcpConnector::connected);
        QSignalSpy failed(&connector, &QcTcpConnector::failed);
        connector.connectTo({u"localhost"_s, listener.serverPort()});
        QTRY_VERIFY_WITH_TIMEOUT(listener.hasPendingConnections() || !failed.isEmpty(), timeoutMs);
        if (!failed.isEmpty()) {
            QFAIL(qPrintable(firstFailure(failed).logText()));
        }
        QcTlsListener::startEncryption(listener.nextPendingConnection());
        QVERIFY(connected.count() == 1 || connected.wait(timeoutMs));
        QVERIFY(connector.takeTransport());
    }

    void anUnknownNameFailsTheLookup()
    {
        // .invalid never resolves (RFC 6761).
        const std::optional<QcJoinFailure> joined = failureJoining({u"no-such-server.invalid"_s, 25600});
        QVERIFY(joined);
        const QcJoinFailure &failure = *joined;
        QCOMPARE(failure.kind, Kind::LookupFailed);
        QVERIFY(failure.tried.isEmpty());
        QCOMPARE(failure.triedText(), u"Address: no-such-server.invalid:25600"_s);
    }
};

QTEST_GUILESS_MAIN(TestJoinFailure)
#include "test_join_failure.moc"
