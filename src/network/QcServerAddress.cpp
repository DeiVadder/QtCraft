#include "QcServerAddress.h"

#include <QHostAddress>

using namespace Qt::StringLiterals;

namespace {

[[nodiscard]] QcResult<quint16> portOf(QStringView text)
{
    bool ok = false;
    const uint port = text.toUInt(&ok);
    if (!ok || port == 0 || port > 65535) {
        return qcError(u"Invalid port '%1'"_s.arg(text));
    }
    return quint16(port);
}

} // namespace

QcResult<QcServerAddress> QcServerAddress::parse(const QString &text)
{
    const QString trimmed = text.trimmed();
    QcServerAddress address;
    if (trimmed.startsWith(u'[')) {
        // [v6] or [v6]:port
        const qsizetype end = trimmed.indexOf(u']');
        const QStringView rest = end < 0 ? QStringView() : QStringView(trimmed).mid(end + 1);
        if (end < 0 || (!rest.isEmpty() && !rest.startsWith(u':'))) {
            return qcError(u"Invalid address '%1'"_s.arg(text));
        }
        address.host = trimmed.mid(1, end - 1);
        if (!rest.isEmpty()) {
            const QcResult<quint16> port = portOf(rest.mid(1));
            if (!port) {
                return qcError(port.error());
            }
            address.port = *port;
        }
    } else if (trimmed.count(u':') == 1) {
        const qsizetype colon = trimmed.indexOf(u':');
        const QcResult<quint16> port = portOf(QStringView(trimmed).mid(colon + 1));
        if (!port) {
            return qcError(port.error());
        }
        address.host = trimmed.left(colon);
        address.port = *port;
    } else {
        // A host name, an IPv4 address or a bare IPv6 address (several colons, no port).
        address.host = trimmed;
    }

    const bool ipv6 = address.host.contains(u':');
    if (address.host.isEmpty() || address.host.contains(u' ') || (ipv6 && QHostAddress(address.host).isNull())) {
        return qcError(u"Invalid address '%1'"_s.arg(text));
    }
    return address;
}

QString QcServerAddress::toString() const
{
    return host.contains(u':') ? u"[%1]:%2"_s.arg(host).arg(port) : u"%1:%2"_s.arg(host).arg(port);
}
