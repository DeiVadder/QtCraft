#pragma once

#include "common/core/QcResult.h"

#include <QByteArray>
#include <QString>

// Files that hold secrets (the player key, the server's TLS key, the known servers, a server's
// settings and identities; decisions E-252, E-872): written atomically (QSaveFile) and readable
// only by the user from the moment the file exists, before any byte of the secret is in it.
namespace QcSecretFile {

// Writes `bytes` to `path` with owner-only permissions; the error is the reason only (the caller
// names the file). Fails when the permissions cannot be set: nothing is written then.
[[nodiscard]] QcStatus write(const QString &path, const QByteArray &bytes);
// Whether the group or others may read the file (a key copied with the wrong permissions).
[[nodiscard]] bool othersMayRead(const QString &path);

} // namespace QcSecretFile
