#pragma once

#include "QcResult.h"

#include <QByteArray>
#include <QString>

// The random key of one installation (decision E-252): 32 bytes, made on first use and kept as
// hex in <data>/player.key, readable only by the user. Servers never see it, only an identity
// derived from it per server (QcLoginCrypto::identity). Whoever copies the file is the player.
namespace QcPlayerKey {

inline constexpr char fileName[] = "player.key";

// <data root>/player.key (QcDataRoot).
QString defaultPath();
// Reads the key; when the file does not exist, makes a new key and writes it. A damaged file is an
// error (it is not replaced: the identity on every server hangs on it).
QcResult<QByteArray> loadOrCreate(const QString &path);

} // namespace QcPlayerKey
