// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

// Access lists of secret files on Windows (decision E-901), where Qt's permission bits only set
// the read-only attribute: a file inherits the access list of its folder unless it gets its own.
// Without effect on macOS and Linux (QcSecretFile uses the permission bits there).
namespace QcOwnerOnlyAccess {

// Gives the file a protected access list that lets only the current user in (nothing inherited
// from the folder); false when the file system has no access lists (FAT) or the call fails.
[[nodiscard]] bool restrictToOwner(const QString &path);
// Whether the file's access list lets Everyone, Users or Authenticated Users read it.
[[nodiscard]] bool othersMayRead(const QString &path);

} // namespace QcOwnerOnlyAccess
