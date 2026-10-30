// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

/*! \brief The few cryptographic steps of the minimal login (decision E-252), all HMAC-SHA256 from
    QtCore: a password key stretched with PBKDF2, a player identity per server derived from the
    player's installation key, proofs bound to the nonce of one connection and a short fingerprint
    of an identity.

    Nothing here encrypts the connection. */
namespace QcLoginCrypto {

/*! The size of a key and an identity (an HMAC-SHA256 output). */
inline constexpr qsizetype keyBytes = 32;
/*! PBKDF2 rounds of the password key: the client pays them once per login, the server once per
    start; they slow down guessing a password from a recorded login. */
inline constexpr int passwordRounds = 20'000;
/*! The bytes of SHA-256 a fingerprint keeps. */
inline constexpr qsizetype fingerprintBytes = 16;

/*! Returns \a count cryptographically strong random bytes of the system. */
QByteArray randomBytes(qsizetype count);
/*! Returns the HMAC-SHA256 of \a data with \a key. */
QByteArray hmac(QByteArrayView key, QByteArrayView data);
/*! Returns PBKDF2-HMAC-SHA256 of \a password and \a salt over \a rounds, with one output block
    (32 bytes). */
QByteArray pbkdf2(QByteArrayView password, QByteArrayView salt, int rounds);
/*! Returns the key both sides derive from the server \a password and the server's \a salt; empty
    without a password. */
QByteArray passwordKey(const QString &password, QByteArrayView salt);
/*! Returns the player's identity on a server: derived from \a installationKey and the server's
    \a salt, so no two servers see the same identity and none learns the installation key. */
QByteArray identity(QByteArrayView installationKey, QByteArrayView salt);

/*! What a proof proves. */
enum class Purpose {
    /*! The server password. */
    Password,
    /*! The player's identity. */
    Identity
};
/*! Returns the HMAC of the connection's \a nonce with \a key, separated by \a purpose. */
QByteArray proof(QByteArrayView key, QByteArrayView nonce, Purpose purpose);
/*! Returns whether \a a and \a b are equal, in time independent of where the bytes differ. */
bool equal(QByteArrayView a, QByteArrayView b);
/*! Returns the lower-case hex of the first fingerprintBytes of SHA-256 of \a identity: shown to
    operators and kept in access.json. */
QString fingerprint(QByteArrayView identity);

} // namespace QcLoginCrypto
