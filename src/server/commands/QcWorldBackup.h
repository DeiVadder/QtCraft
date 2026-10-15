#pragma once

#include "QcResult.h"

#include <QDateTime>
#include <QString>

// A copy of a saved world folder (decisions E-246, E-256). The server saves first and takes a
// snapshot between two ticks: every file is hard-linked into a hidden staging folder in the backup
// root, which takes milliseconds. The server writes its files only by replacing them (QSaveFile),
// never in place, so the linked files keep the saved state while the game goes on; a background
// thread then copies the staging folder into the backup and removes it. Where hard links are not
// possible (another file system, no support) the snapshot copies instead and the tick waits as
// before. The target is <backup root>/<world folder>-<yyyyMMdd-HHmmss>[-<label>]; the label is
// limited to letters, digits, '_' and '-', so no path can leave the root, and a root inside the
// world folder is refused. The session lock is not copied, symbolic links are refused, the copy
// needs the world's size plus a margin of free disk space, and a copy that fails halfway is
// removed again (it is written under a hidden name and renamed when complete).
namespace QcWorldBackup {

// Worlds larger than this are refused (the copy blocks the server tick).
inline constexpr qint64 maxWorldBytes = qint64(2) * 1024 * 1024 * 1024;
// Kept free on the target disk besides the copy.
inline constexpr qint64 reserveBytes = qint64(256) * 1024 * 1024;
inline constexpr qsizetype maxLabelLength = 32;

struct Summary
{
    QString directory;
    int files = 0;
    qint64 bytes = 0;
};

// A world frozen for its copy: the staging folder with its files and where the backup goes.
struct Snapshot
{
    QString staging;
    QString target;
    QStringList files;
    qint64 bytes = 0;
    // Whether the files are hard links (else copies: the snapshot took the copy's time).
    bool linked = true;
};

// The default root for a world directory: "backups" next to the folder of worlds
// (<data>/worlds/<world> -> <data>/backups).
QString defaultRoot(const QString &worldDirectory);
// Whether a label may be used: empty, or up to maxLabelLength letters, digits, '_' and '-'.
bool isValidLabel(const QString &label);
// Checks the request and freezes the world's files (in the server's thread, after saving).
[[nodiscard]] QcResult<Snapshot> snapshot(const QString &worldDirectory, const QString &root, const QString &label, const QDateTime &now);
// Copies a snapshot into its target and removes the staging folder (any thread). On failure no
// backup and no staging folder is left.
[[nodiscard]] QcResult<Summary> complete(const Snapshot &snapshot);
// Both in one go.
[[nodiscard]] QcResult<Summary> copy(const QString &worldDirectory, const QString &root, const QString &label, const QDateTime &now);

} // namespace QcWorldBackup
