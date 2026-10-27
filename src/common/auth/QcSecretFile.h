#pragma once

#include "common/core/QcResult.h"

#include <QByteArray>
#include <QString>

// Files that hold secrets (the player key, the server's TLS key, the known servers, a server's
// settings and identities; decisions E-252, E-872, E-901): written atomically (QSaveFile) and
// readable only by the user. On macOS and Linux that holds from the moment the file exists, before
// any byte of the secret is in it; on Windows the file gets an owner-only access list right after
// it is written, until then the access list of its folder applies (the folder in the user's
// profile is private to the user, a folder in the root of a drive is not).
namespace QcSecretFile {

// Writes `bytes` to `path` with owner-only permissions; the error is the reason only (the caller
// names the file). Fails when the permissions cannot be set: nothing is written then. On Windows a
// failed owner-only access list (a drive without access lists) only warns.
[[nodiscard]] QcStatus write(const QString &path, const QByteArray &bytes);
// Whether the group or others may read the file (a key copied with the wrong permissions); on
// Windows whether its access list lets Everyone, Users or Authenticated Users read it.
[[nodiscard]] bool othersMayRead(const QString &path);

} // namespace QcSecretFile
