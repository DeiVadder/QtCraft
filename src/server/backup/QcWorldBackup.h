// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"

#include <QDateTime>
#include <QString>

/*! \brief A copy of a saved world folder (decisions E-246, E-256).

    The server saves first and takes a snapshot between two ticks: every file is hard-linked into
    a hidden staging folder in the backup root, which takes milliseconds. The server writes its
    files only by replacing them (QSaveFile), never in place, so the linked files keep the saved
    state while the game goes on; a background thread then copies the staging folder into the
    backup and removes it. Where hard links are not possible (another file system, no support)
    the snapshot copies instead and the tick waits as before. The target is <backup root>/<world
    folder>-<yyyyMMdd-HHmmss>[-<label>]; the label is limited to letters, digits, '_' and '-', so
    no path can leave the root, and a root inside the world folder is refused. The session lock
    is not copied, symbolic links are refused, the copy needs the world's size plus a margin of
    free disk space, and a copy that fails halfway is removed again (it is written under a hidden
    name and renamed when complete). */
namespace QcWorldBackup {

/*! Worlds larger than this are refused (the snapshot copies them in the tick where hard links
    fail). */
inline constexpr qint64 maxWorldBytes = qint64{2} * 1024 * 1024 * 1024;
/*! Kept free on the target disk besides the copy. */
inline constexpr qint64 reserveBytes = qint64{256} * 1024 * 1024;
/*! The longest label a backup may carry. */
inline constexpr qsizetype maxLabelLength = 32;

/*! \brief A finished backup: its folder, the number of files and their size in bytes. \qcfields */
struct Summary
{
    QString directory;
    int files = 0;
    qint64 bytes = 0;
};

/*! \brief A world frozen for its copy: the staging folder with its files and where the backup
    goes.

    The staging folder, the target folder, the files relative to the world folder and their size in
    bytes. \qcfields */
struct Snapshot
{
    QString staging;
    QString target;
    QStringList files;
    qint64 bytes = 0;
    /*! Whether the files are hard links (else copies: the snapshot took the copy's time). */
    bool linked = true;
};

/*! Returns the default root for \a worldDirectory: "backups" next to the folder of worlds
    (<data>/worlds/<world> -> <data>/backups). */
QString defaultRoot(const QString &worldDirectory);
/*! Returns whether \a label may be used: empty, or up to maxLabelLength letters, digits, '_' and
    '-'. */
bool isValidLabel(const QString &label);
/*! Checks the request and freezes the files of \a worldDirectory for a backup below \a root
    named after \a now and \a label (in the server's thread, after saving). Returns the snapshot or
    why it was refused. */
[[nodiscard]] QcResult<Snapshot> snapshot(const QString &worldDirectory, const QString &root, const QString &label, const QDateTime &now);
/*! Copies \a snapshot into its target and removes the staging folder (any thread). On failure no
    backup and no staging folder is left. Returns the summary or the error. */
[[nodiscard]] QcResult<Summary> complete(const Snapshot &snapshot);
/*! Takes the snapshot of \a worldDirectory for \a root, \a label and \a now and completes it in
    one go. Returns the summary or the error. */
[[nodiscard]] QcResult<Summary> copy(const QString &worldDirectory, const QString &root, const QString &label, const QDateTime &now);

} // namespace QcWorldBackup
