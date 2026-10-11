#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

// The few cryptographic steps of the minimal login (decision E-252), all HMAC-SHA256 from QtCore:
// a password key stretched with PBKDF2, a player identity per server derived from the player's
// installation key, proofs bound to the nonce of one connection and a short fingerprint of an
// identity. Nothing here encrypts the connection.
namespace QcLoginCrypto {

inline constexpr qsizetype keyBytes = 32;
// PBKDF2 rounds of the password key: the client pays them once per login, the server once per
// start; they slow down guessing a password from a recorded login.
inline constexpr int passwordRounds = 20'000;
inline constexpr qsizetype fingerprintBytes = 16;

// Cryptographically strong random bytes of the system.
QByteArray randomBytes(qsizetype count);
QByteArray hmac(QByteArrayView key, QByteArrayView data);
// PBKDF2-HMAC-SHA256 with one output block (32 bytes).
QByteArray pbkdf2(QByteArrayView password, QByteArrayView salt, int rounds);
// The key both sides derive from the server password and the server's salt; empty without one.
QByteArray passwordKey(const QString &password, QByteArrayView salt);
// The player's identity on a server: derived from the installation key and the server's salt, so
// no two servers see the same identity and none learns the installation key.
QByteArray identity(QByteArrayView installationKey, QByteArrayView salt);

enum class Purpose { Password, Identity };
// HMAC of the connection's nonce with `key`, separated by purpose.
QByteArray proof(QByteArrayView key, QByteArrayView nonce, Purpose purpose);
// Compares in time independent of where the bytes differ.
bool equal(QByteArrayView a, QByteArrayView b);
// Lower-case hex of the first fingerprintBytes of SHA-256 of an identity: shown to operators and
// kept in access.json.
QString fingerprint(QByteArrayView identity);

} // namespace QcLoginCrypto
