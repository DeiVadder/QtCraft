// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/multiplayer/QcLanAddresses.h"

#include <QNetworkInterface>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// Loopback reaches only this computer (listed only on request); link-local addresses (169.254.x.x, fe80::) need a zone and
// rarely work between computers.
bool listed(const QHostAddress &address, bool includeLoopback)
{
    return !address.isNull() && (includeLoopback || !address.isLoopback()) && !address.isLinkLocal() && !address.isMulticast()
           && address != QHostAddress::AnyIPv4 && address != QHostAddress::AnyIPv6;
}

} // namespace

QStringList QcLanAddresses::current(quint16 port)
{
    QList<QHostAddress> addresses;
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &interface : interfaces) {
        const QNetworkInterface::InterfaceFlags flags = interface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) || !flags.testFlag(QNetworkInterface::IsRunning)
            || flags.testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }

        const QList<QNetworkAddressEntry> entries = interface.addressEntries();
        for (const QNetworkAddressEntry &entry : entries) {
            // Temporary IPv6 addresses change every few hours.
            if (!entry.isTemporary()) {
                addresses.append(entry.ip());
            }
        }
    }
    return describe(addresses, port);
}

QStringList QcLanAddresses::describe(const QList<QHostAddress> &addresses, quint16 port, bool includeLoopback)
{
    QList<QHostAddress> usable;
    for (const QHostAddress &address : addresses) {
        // Without the interface's scope; it means nothing on another computer.
        QHostAddress plain(address);
        plain.setScopeId({});
        if (listed(plain, includeLoopback) && !usable.contains(plain)) {
            usable.append(plain);
        }
    }

    // IPv4 first (what most players type), each family in a stable order.
    std::ranges::stable_sort(usable, [](const QHostAddress &a, const QHostAddress &b) {
        const bool aV4 = a.protocol() == QAbstractSocket::IPv4Protocol;
        const bool bV4 = b.protocol() == QAbstractSocket::IPv4Protocol;
        return aV4 != bV4 ? aV4 : a.toString() < b.toString();
    });

    QStringList described;
    for (const QHostAddress &address : std::as_const(usable)) {
        if (described.size() == maxAddresses) {
            break;
        }
        const bool v4 = address.protocol() == QAbstractSocket::IPv4Protocol;
        described.append((v4 ? u"%1:%2"_s : u"[%1]:%2"_s).arg(address.toString()).arg(port));
    }
    return described;
}
