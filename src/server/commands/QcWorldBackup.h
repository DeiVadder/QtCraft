#pragma once

#include "QcResult.h"

#include <QDateTime>
#include <QString>

// A copy of a saved world folder (decision E-246). The server saves first and copies in its own
// thread between two ticks, so no file changes during the copy and the copy opens like the world
// itself; the ticks wait meanwhile. The target is <backup root>/<world folder>-<yyyyMMdd-HHmmss>
// [-<label>]; the label is limited to letters, digits, '_' and '-', so no path can leave the root,
// and a root inside the world folder is refused. The session lock is not copied, symbolic links are
// refused, the copy needs the world's size plus a margin of free disk space, and a copy that fails
// halfway is removed again.
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

// The default root for a world directory: "backups" next to the folder of worlds
// (<data>/worlds/<world> -> <data>/backups).
QString defaultRoot(const QString &worldDirectory);
// Whether a label may be used: empty, or up to maxLabelLength letters, digits, '_' and '-'.
bool isValidLabel(const QString &label);
QcResult<Summary> copy(const QString &worldDirectory, const QString &root, const QString &label, const QDateTime &now);

} // namespace QcWorldBackup
