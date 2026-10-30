// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

/*! \brief Access lists of secret files on Windows (decision E-901), where Qt's permission bits only
    set the read-only attribute: a file inherits the access list of its folder unless it gets its
    own.

    Without effect on macOS and Linux (QcSecretFile uses the permission bits there). */
namespace QcOwnerOnlyAccess {

/*! Gives the file at \a path a protected access list that lets only the current user in (nothing
    inherited from the folder). Returns false when the file system has no access lists (FAT) or
    the call fails; true without doing anything outside Windows. */
[[nodiscard]] bool restrictToOwner(const QString &path);
/*! Returns whether the access list of the file at \a path lets Everyone, Users or Authenticated
    Users read it; false outside Windows. */
[[nodiscard]] bool othersMayRead(const QString &path);

} // namespace QcOwnerOnlyAccess
