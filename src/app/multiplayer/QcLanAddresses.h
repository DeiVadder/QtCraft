// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QHostAddress>
#include <QList>
#include <QStringList>

// The addresses other computers can use to join a world opened to the network (decision E-260):
// the addresses of this computer's running network interfaces without loopback and link-local
// ones and temporary (privacy) IPv6 addresses, IPv4 first, each as "address:port" ("[v6]:port").
class QcLanAddresses
{
public:
    // The current addresses of this computer.
    static QStringList current(quint16 port);
    // At most this many addresses are listed.
    static constexpr qsizetype maxAddresses = 4;

    // `addresses` filtered, sorted and written with `port`; duplicates removed. `includeLoopback`
    // keeps loopback addresses (a listener bound to this computer only).
    static QStringList describe(const QList<QHostAddress> &addresses, quint16 port, bool includeLoopback = false);
};
