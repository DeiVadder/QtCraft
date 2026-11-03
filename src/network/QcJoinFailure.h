// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "network/QcServerAddress.h"

#include <QAbstractSocket>
#include <QCoreApplication>
#include <QHostAddress>
#include <QList>
#include <QMetaType>
#include <QString>
#include <QStringList>

/*! \brief Why joining a server failed (decision E-964): the step that failed as a kind, the
    address and the IP addresses tried, a translated reason and a hint what to check.

    The texts come from the translation (context "QcJoinFailure"), never from the kind's name;
    detail and steps keep Qt's English error texts for the log. */
struct QcJoinFailure
{
    Q_GADGET
    Q_DECLARE_TR_FUNCTIONS(QcJoinFailure)

public:
    /*! The step of a join that failed. */
    enum class Kind {
        Other,         // anything else; reason says what
        LookupFailed,   // the server's name was not found
        LookupTimedOut, // looking the server's name up took too long (slow or no name server)
        TimedOut,      // no address answered the connection in time
        Refused,       // nothing listens on the port (the address answered with a refusal)
        Unreachable,   // the network cannot reach the address (or its family, as IPv6 without IPv6)
        Blocked,       // the system does not allow the connection (firewall, permissions)
        NoAnswer,      // the connection stands, but the TLS handshake got no answer in time
        OtherVersion,  // the server hung up on the TLS handshake (a version without TLS)
        TlsFailed,     // the TLS handshake failed otherwise
        NoCertificate, // the server showed no certificate
        Untrusted,     // the server's certificate is not trusted (pin changed or not checkable)
        ServerClosed,  // the server closed the connection while logging in, without a reason
        LoginRefused   // the server refused the login with a reason (password, version, content)
    };
    Q_ENUM(Kind)

    /*! The step a socket error happened in. */
    enum class Step {
        Lookup,     // looking up the server's name
        Connecting, // making the TCP connection to one of its addresses
        Handshake   // the TCP connection stands, the TLS handshake runs
    };
    Q_ENUM(Step)

    /*! The step that failed. */
    Kind kind = Kind::Other;
    /*! The address the player joined; empty host for failures before an address was known. */
    QcServerAddress address;
    /*! The IP addresses a connection was tried to, in order (after the name lookup). */
    QList<QHostAddress> tried;
    /*! What failed, translated, for the player. */
    QString reason;
    /*! Qt's error text of the last error (English), for the log. */
    QString detail;
    /*! What happened at each address tried (English), for the log. */
    QStringList steps;

    /*! Returns the kind of the socket \a error that happened in \a step; a timeout of the
        connector counts as QAbstractSocket::SocketTimeoutError. */
    [[nodiscard]] static Kind kindOf(QAbstractSocket::SocketError error, Step step);
    /*! Returns the translated reason for \a kind at \a host; empty for the kinds whose reason
        comes from elsewhere (Other, Untrusted, LoginRefused). */
    [[nodiscard]] static QString reasonFor(Kind kind, const QString &host);
    /*! Returns the failure of \a kind at \a address with its reasonFor() text, or \a detail where
        the kind has none. */
    [[nodiscard]] static QcJoinFailure of(Kind kind, const QcServerAddress &address, const QString &detail = {});

    /*! Returns a short translated hint what to check; empty when the reason says it all. */
    QString hint() const;
    /*! Returns the translated line naming the address and the IP addresses tried ("Tried …");
        empty without an address. */
    QString triedText() const;
    /*! Returns everything in one English line for the log: kind, address, addresses, steps. */
    QString logText() const;
    /*! Returns whether connections were tried and every one went to an IPv6 address. */
    bool triedOnlyIpv6() const;
};

Q_DECLARE_METATYPE(QcJoinFailure)
