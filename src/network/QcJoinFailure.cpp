// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "network/QcJoinFailure.h"

#include <QMetaEnum>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

QcJoinFailure::Kind connectingKind(QAbstractSocket::SocketError error)
{
    using Kind = QcJoinFailure::Kind;
    switch (error) {
    case QAbstractSocket::ConnectionRefusedError:
        return Kind::Refused;
    // ENETUNREACH, EHOSTUNREACH and their Windows twins are NetworkError; an address family the
    // system has no stack for (IPv6 switched off) is an unsupported operation.
    case QAbstractSocket::NetworkError:
    case QAbstractSocket::UnsupportedSocketOperationError:
        return Kind::Unreachable;
    case QAbstractSocket::SocketTimeoutError:
        return Kind::TimedOut;
    case QAbstractSocket::RemoteHostClosedError:
        return Kind::ServerClosed;
    case QAbstractSocket::SocketAccessError:
        return Kind::Blocked;
    case QAbstractSocket::HostNotFoundError:
        return Kind::LookupFailed;
    default:
        return Kind::Other;
    }
}

QcJoinFailure::Kind handshakeKind(QAbstractSocket::SocketError error)
{
    using Kind = QcJoinFailure::Kind;
    switch (error) {
    // A server of a version before TLS reads the handshake as a frame too large and hangs up.
    case QAbstractSocket::SslHandshakeFailedError:
    case QAbstractSocket::RemoteHostClosedError:
        return Kind::OtherVersion;
    case QAbstractSocket::SocketTimeoutError:
        return Kind::NoAnswer;
    default:
        return Kind::TlsFailed;
    }
}

// "203.0.113.5:25600 (IPv4)", "[2001:db8::1]:25600 (IPv6)".
QString endpoint(const QHostAddress &address, quint16 port)
{
    const bool v6 = address.protocol() == QAbstractSocket::IPv6Protocol;
    const QString host = v6 ? u"[%1]"_s.arg(address.toString()) : address.toString();
    return u"%1:%2 (%3)"_s.arg(host).arg(port).arg(v6 ? u"IPv6"_s : u"IPv4"_s);
}

// Every address of `tried` with `port`, in order.
QStringList endpoints(const QList<QHostAddress> &tried, quint16 port)
{
    QStringList list;
    for (const QHostAddress &ip : tried) {
        list.append(endpoint(ip, port));
    }
    return list;
}

} // namespace

QcJoinFailure::Kind QcJoinFailure::kindOf(QAbstractSocket::SocketError error, Step step)
{
    switch (step) {
    case Step::Lookup:
        return error == QAbstractSocket::SocketTimeoutError ? Kind::LookupTimedOut : Kind::LookupFailed;
    case Step::Connecting:
        return connectingKind(error);
    case Step::Handshake:
        return handshakeKind(error);
    }
    return Kind::Other;
}

QString QcJoinFailure::reasonFor(Kind kind, const QString &host)
{
    switch (kind) {
    case Kind::LookupFailed:
        return tr("The server name %1 was not found").arg(host);
    case Kind::LookupTimedOut:
        return tr("Looking up the server name %1 took too long").arg(host);
    case Kind::TimedOut:
        return tr("The connection timed out: the server did not answer");
    case Kind::Refused:
        return tr("Nobody accepts connections at this address and port");
    case Kind::Unreachable:
        return tr("Your network cannot reach this address");
    case Kind::Blocked:
        return tr("The system does not allow this connection");
    case Kind::NoAnswer:
        return tr("The server took the connection but did not answer");
    case Kind::OtherVersion:
        return tr("The server did not set up an encrypted connection");
    case Kind::TlsFailed:
        return tr("The encrypted connection could not be set up");
    case Kind::NoCertificate:
        return tr("The server showed no certificate");
    case Kind::ServerClosed:
        return tr("The server closed the connection");
    case Kind::Other:
    case Kind::Untrusted:
    case Kind::LoginRefused:
        break;
    }
    return {};
}

QcJoinFailure QcJoinFailure::of(Kind kind, const QcServerAddress &address, const QString &detail)
{
    const QString reason = reasonFor(kind, address.host);
    return {.kind = kind, .address = address, .tried = {}, .reason = reason.isEmpty() ? detail : reason, .detail = detail, .steps = {}};
}

QString QcJoinFailure::hint() const
{
    switch (kind) {
    case Kind::LookupFailed:
        return tr("Check the address for typos and your internet connection.");
    case Kind::LookupTimedOut:
        return tr("Your internet connection or its name server (DNS) may be slow or down: try again, or use the server's IP "
                  "address.");
    case Kind::TimedOut:
        return triedOnlyIpv6() ? tr("Is the server running and its port forwarded? An IPv6 address needs IPv6 on your side too: "
                                    "try the server's IPv4 address or a VPN with IPv6.")
                               : tr("Is the server running and its port forwarded? A firewall may drop the connection.");
    case Kind::Refused:
        return tr("Is the server running, and on this port?");
    case Kind::Unreachable:
        return triedOnlyIpv6() ? tr("Your network may have no IPv6: try the server's IPv4 address or a VPN with IPv6.")
                               : tr("Check your network connection and VPN.");
    case Kind::Blocked:
        return tr("A firewall or security program on your computer may block QtCraft.");
    case Kind::NoAnswer:
        return tr("Is a QtCraft server of the same version running on this port?");
    case Kind::OtherVersion:
    case Kind::TlsFailed:
    case Kind::NoCertificate:
        return tr("You and the server need the same version of QtCraft.");
    case Kind::ServerClosed:
        return tr("The server may be full or restarting, or it does not let your address in.");
    case Kind::Other:
    case Kind::Untrusted:
    case Kind::LoginRefused:
        break;
    }
    return {};
}

QString QcJoinFailure::triedText() const
{
    if (address.host.isEmpty()) {
        return {};
    }
    if (tried.isEmpty()) {
        return tr("Address: %1").arg(address.toString());
    }

    const QString list = endpoints(tried, address.port).join(u", "_s);
    // A name: say which addresses it stands for.
    if (QHostAddress(address.host).isNull()) {
        return tr("Tried %1 at %2").arg(address.host, list);
    }
    return tr("Tried %1").arg(list);
}

QString QcJoinFailure::logText() const
{
    const QStringList ips = endpoints(tried, address.port);
    const char *const name = QMetaEnum::fromType<Kind>().valueToKey(static_cast<int>(kind));
    QString text = u"Joining %1 failed (%2): %3"_s.arg(address.toString(), QString::fromLatin1(name), reason);
    if (!ips.isEmpty()) {
        text += u"; tried %1"_s.arg(ips.join(u", "_s));
    }
    if (!steps.isEmpty()) {
        text += u"; %1"_s.arg(steps.join(u"; "_s));
    }
    if (!detail.isEmpty() && detail != reason) {
        text += u"; system: %1"_s.arg(detail);
    }
    return text;
}

bool QcJoinFailure::triedOnlyIpv6() const
{
    return !tried.isEmpty()
           && std::ranges::all_of(tried, [](const QHostAddress &ip) { return ip.protocol() == QAbstractSocket::IPv6Protocol; });
}
